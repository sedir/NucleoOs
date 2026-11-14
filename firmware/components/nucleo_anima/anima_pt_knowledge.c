// Portuguese frozen knowledge: streamed APK1 aliases + answer blob on SD.
// No resident corpus, model, heap allocation or static writable state. A query
// must cover ALL its salient terms; negations, dates and unknown names cannot
// disappear into a fuzzy nearest neighbour. See tools/anima/pt/README.md.
#include "anima_internal.h"
#include "nucleo_board.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "anima_pt_lexicon.h"
#define PTK_PATH NUCLEO_SD_MOUNT "/data/anima/anima-pt-knowledge.bin"
#define PTK_TERMS 16

static int read_u32(FILE *f, uint32_t *out)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    *out = (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24;
    return 1;
}
static int read_u16(FILE *f, unsigned *out)
{
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    *out = b[0] | (unsigned)b[1]<<8;
    return 1;
}
static char fold_c3(unsigned char d)
{
    if ((d>=0x80&&d<=0x85)||(d>=0xa0&&d<=0xa5)) return 'a';
    if (d==0x87||d==0xa7) return 'c';
    if ((d>=0x88&&d<=0x8b)||(d>=0xa8&&d<=0xab)) return 'e';
    if ((d>=0x8c&&d<=0x8f)||(d>=0xac&&d<=0xaf)) return 'i';
    if (d==0x91||d==0xb1) return 'n';
    if ((d>=0x92&&d<=0x96)||(d>=0xb2&&d<=0xb6)) return 'o';
    if ((d>=0x99&&d<=0x9c)||(d>=0xb9&&d<=0xbc)) return 'u';
    if (d==0x9d||d==0xbd||d==0xbf) return 'y';
    return 0;
}
// Same tokenization/canonicalization as tools/anima/pt/build.py. Refuse overflow
// instead of dropping a final unknown subject and answering a different question.
static int query_terms(const char *text, uint64_t *hashes)
{
    char word[48]; int len=0, n=0, chars=0;
    for (const unsigned char *p=(const unsigned char *)text;;) {
        unsigned char c=*p; char ch=0;
        if (++chars>512) return -1;
        if (c==0xc3&&p[1]) { ch=fold_c3(p[1]); if (!ch) return -1; p+=2; }
        else if (c==0xcc&&p[1]>=0x80&&p[1]<=0xbf) { p+=2; continue; } // NFD combining accents
        else if (c>=0x80) return -1;
        else { if (c&&isalnum(c)) ch=(char)tolower(c); if (c) p++; }
        if (ch) { if (len>=(int)sizeof word-1) return -1; word[len++]=ch; }
        else if (len) {
            word[len]=0; len=0; bool stop=false;
            for (int i=0;PTK_STOP[i];i++) if (!strcmp(word,PTK_STOP[i])) { stop=true;break; }
            if (!stop) {
                const char *term=word;
                for (int i=0;PTK_SYN[i].from;i++) if (!strcmp(word,PTK_SYN[i].from)) { term=PTK_SYN[i].to;break; }
                uint64_t h=UINT64_C(14695981039346656037);
                for (const unsigned char *t=(const unsigned char *)term;*t;t++) h=(h^*t)*UINT64_C(1099511628211);
                bool seen=false; for(int i=0;i<n;i++) if(hashes[i]==h) seen=true;
                if (!seen) { if(n==PTK_TERMS)return -1;hashes[n++]=h; }
            }
        }
        if (!c) break;
    }
    return n;
}

bool a_pt_knowledge(const char *text, anima_result_t *out)
{
    if (!text||!out) return false;
    uint64_t query[PTK_TERMS]; int nq=query_terms(text,query);
    if(nq<=0) return false;
    FILE *f=fopen(PTK_PATH,"rb"); if(!f)return false;
    char magic[4]; uint32_t count,start,size;
    bool hit=false;
    if(fread(magic,1,4,f)!=4||memcmp(magic,"APK1",4)||!read_u32(f,&count)||!read_u32(f,&start)||!read_u32(f,&size))goto end;
    if(count>100000||start<16||start>size||size>16*1024*1024)goto end;
    if(fseek(f,0,SEEK_END)||ftell(f)!=(long)size||fseek(f,16,SEEK_SET))goto end;
    uint32_t best_id=UINT32_MAX,best_off=0; int best=0,second=0;
    for(uint32_t row=0;row<count;row++) {
        uint32_t id,off; unsigned char n;
        if(!read_u32(f,&id)||!read_u32(f,&off)||fread(&n,1,1,f)!=1||n==0||n>PTK_TERMS||off<start||off>=size)goto end;
        unsigned mask=0;
        for(int j=0;j<n;j++) {
            uint32_t lo,hi; if(!read_u32(f,&lo)||!read_u32(f,&hi))goto end;
            uint64_t h=(uint64_t)hi<<32|lo;
            for(int k=0;k<nq;k++) if(query[k]==h) mask|=1u<<k;
        }
        if(ftell(f)>(long)start)goto end;
        if(mask!=((1u<<nq)-1))continue;
        int score=100*nq/n;
        if(id==best_id) { if(score>best){best=score;best_off=off;} continue; }
        if(score>best) { second=best;best=score;best_id=id;best_off=off; }
        else if(score>second) second=score;
    }
    if(ftell(f)!=(long)start||best<60||best-second<15||!best_off)goto end;
    if(fseek(f,(long)best_off,SEEK_SET))goto end;
    unsigned len;
    if(!read_u16(f,&len)||!len||len>=sizeof out->arg)goto end;
    // Validate metadata before reading the answer into the caller's existing buffer.
    char cid[64];
    if(fread(cid,1,len,f)!=len)goto end;
    cid[len]=0;
    if(!read_u16(f,&len)||!len||len>=sizeof out->reply||(long)len+(long)ftell(f)>(long)size)goto end;
    // The caller invokes this before any other result is produced. Read into its
    // existing result buffer: no extra 1 KB result/answer on the small native stack.
    if(fread(out->reply,1,len,f)!=len)goto end;
    out->reply[len]=0;
    out->tier=ANIMA_TIER_FACT;out->action=ANIMA_ACT_ANSWER;out->confidence=best;
    snprintf(out->intent,sizeof out->intent,"pt_knowledge");
    snprintf(out->arg,sizeof out->arg,"%s",cid);
    snprintf(out->state,sizeof out->state,"idle");
    hit=true;
end:
    fclose(f);return hit;
}

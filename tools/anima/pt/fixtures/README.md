# Portuguese ASR fixtures

Synthetic Brazilian Portuguese speech, generated with pt-BR-FranciscaNeural for
this project's TTS pack, then resampled to 16 kHz mono signed 16-bit PCM. These
three short declarative sentences contain no personal recordings. Their texts
are the filenames with underscores replaced by spaces.

The browser test feeds these WAVs to the real Vosk worker with external networking
blocked. This proves packaging, language selection and transcription work; it is
not a real-speaker accuracy benchmark. The official small Portuguese model has
substantial published error rates; test real accents, noise and the Cardputer
microphone before claiming dictation quality.

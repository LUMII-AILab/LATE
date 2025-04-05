import { extractAudio as extractAudio_FFmpeg } from './ffmpeg.js';

function fallbackCopyTextToClipboard(text) {
  var textArea = document.createElement("textarea");
  textArea.value = text;

  // Hide the textarea off-screen
  textArea.style.position = "fixed";
  textArea.style.top = "0";
  textArea.style.left = "0";
  textArea.style.width = "1px";
  textArea.style.height = "1px";
  textArea.style.opacity = "0";
  document.body.appendChild(textArea);

  textArea.focus();
  textArea.select();

  let successful;

  try {
      successful = document.execCommand('copy');
      if (successful) {
          console.log('Text copied to clipboard successfully');
      } else {
          console.error('Error copying text to clipboard');
      }
  } catch (err) {
      console.error('Error copying text to clipboard: ', err);
  }

  document.body.removeChild(textArea);

  return successful;
}

export async function copyTextToClipboard(text) {
  // Attempt to use the modern navigator.clipboard API
  if (navigator.clipboard) {
      return await navigator.clipboard.writeText(text)
          .then(function() {
              console.log('Text copied to clipboard successfully');
          })
          .catch(function(error) {
              console.error('Error copying text to clipboard: ', error);
              // Fallback to using execCommand for older browsers
              fallbackCopyTextToClipboard(text);
          });
  } else {
      // Fallback to using execCommand for browsers that do not support navigator.clipboard
      return fallbackCopyTextToClipboard(text);
  }
}

export async function saveAs(blob, filename) {
  var a = document.createElement("a");
  a.href = typeof blob === 'string' ? blob : window.URL.createObjectURL(blob);
  a.download = filename || 'output';
  a.click();
  a.remove();
  // const pickerOptions = {
  //   suggestedName: filename || 'output',
  //   types: [
  //     {
  //       description: 'Save File',
  //       accept: {
  //         // 'text/plain': ['.txt'],
  //       },
  //     },
  //   ],
  // };
  // const fileHandle = await window.showSaveFilePicker(pickerOptions);
  // const fileStream = await fileHandle.createWritable();
  // await fileStream.write(blob);
  // await fileStream.close();
}

export function toSRTtime(ms) { let seconds = Math.floor(ms / 100.0);
  ms %= 1000;
  let minutes = Math.floor(seconds / 60);
  seconds %= 60;
  let hours = Math.floor(minutes / 60);
  minutes %= 60;
  return `${hours}:${('00'+minutes).slice(-2)}:${('00'+seconds).slice(-2)},${('000'+ms).slice(-3)}`;
}

export function msToTime(s, {ms=false, hours=true}={}) {
  if(s == undefined || isNaN(s)) {
    return `${hours ? '--:' : ''}--:--${ms ? '.---' : ''}`;
  }
  function pad(n, z) {
    z = z || 2;
    return ('00' + n).slice(-z);
  }
  var _ms = s % 1000;
  s = (s - _ms) / 1000;
  var secs = s % 60;
  s = (s - secs) / 60;
  var mins = s % 60;
  var hrs = (s - mins) / 60;
  return ((hrs || hours) ? (pad(hrs) + ':') : '') + pad(mins) + ':' + pad(secs) + (ms ? '.' + pad(_ms, 3) : '');
}

// https://github.com/pdeschen/pcm.js/blob/master/pcm.js
function u32ToArray(i) {
  return Uint8Array.from([i&0xFF, (i>>8)&0xFF, (i>>16)&0xFF, (i>>24)&0xFF]);
}

function u16ToArray(i) {
  return Uint8Array.from([i&0xFF, (i>>8)&0xFF]);
}

export function buffer2wav(audioBuffer, duration) {
  // fileSize
  // 36 + SubChunk2Size, or more precisely:
  //                      4 + (8 + SubChunk1Size) + (8 + SubChunk2Size)
  //                      This is the size of the rest of the chunk 
  //                      following this number.  This is the size of the 
  //                      entire file in bytes minus 8 bytes for the
  //                      two fields not included in this count:
  //                      ChunkID and ChunkSize.

  const bitsPerSample = 16;
  const numberOfChannels = audioBuffer.numberOfChannels;
  const sampleRate = audioBuffer.sampleRate;
  let sampleCount = audioBuffer.length;

  if(duration > 0) {
    sampleCount = duration * audioBuffer.sampleRate;
  }

  const ch = numberOfChannels; // 1;
  const fmt = 1; //pcm
  const srate = sampleRate;
  // const bitsPerSample = sampleSize; // 16;
  const byteRate = srate * ch * bitsPerSample/8; // == SampleRate * NumChannels * BitsPerSample/8
  const blockAlign = ch * bitsPerSample/8; // == NumChannels * BitsPerSample/8 ; The number of bytes for one sample including all channels.
  // if(!sampleCount) {
  //   sampleCount = data.length / (bitsPerSample/8) / numberOfChannels;
  // }
  // I wonder what happens when this number isn't an integer?
  const subchunk1Size = 16; // 16 for PCM.  This is the size of the rest of the Subchunk which follows this number.
  const subchunk2Size = sampleCount * ch * bitsPerSample/8; // == NumSamples * NumChannels * BitsPerSample/8
  const chunkSize = 4 + 8 + subchunk1Size + 8 + subchunk2Size; // 4 + (8 + SubChunk1Size) + (8 + SubChunk2Size)
  // console.log(data.length, sampleCount, sampleCount*2)
  // const dblob = new Blob(data);
  const hblob = new Blob([
    "RIFF", u32ToArray(chunkSize), "WAVE",
    "fmt ", u32ToArray(subchunk1Size),
    u16ToArray(fmt), u16ToArray(ch), u32ToArray(srate),
    u32ToArray(byteRate),
    u16ToArray(blockAlign),
    u16ToArray(bitsPerSample),
    "data", u32ToArray(subchunk2Size),
    ], { type: '' });

  const data = new Int16Array(sampleCount * numberOfChannels);  // only if bits per sample = 16

  const channels = [];
  for(let i = 0; i < numberOfChannels; i++) {
    channels.push(audioBuffer.getChannelData(i));
  }

  for(let i = 0; i < sampleCount; i++) {
    for(let j = 0; j < numberOfChannels; j++) {
      data[i * numberOfChannels + j] = channels[j][i] * 0x7fff /*0x8000*/;
    }
  }

  const dblob = new Blob([data]);

  const blob = new Blob([hblob, dblob], { type: 'audio/wav' })

  return blob;
}

function array2wav(audioData, duration, audioBuffer = { numberOfChannels: 1, sampleRate: 16000 }) {
  audioBuffer.length = audioData.length;
  audioBuffer.getChannelData = (i) => { return audioData; };
  console.log('array2wav', audioBuffer)
  return buffer2wav(audioBuffer, duration);
}

function arrays2wav(audioData, duration, audioBuffer = { numberOfChannels: 1, sampleRate: 16000 }) {
  audioBuffer.length = 0;
  for (const arr of audioData) {
    audioBuffer.length += arr.length;
  }
  console.log('arrays2wav', audioBuffer)
  // fileSize
  // 36 + SubChunk2Size, or more precisely:
  //                      4 + (8 + SubChunk1Size) + (8 + SubChunk2Size)
  //                      This is the size of the rest of the chunk 
  //                      following this number.  This is the size of the 
  //                      entire file in bytes minus 8 bytes for the
  //                      two fields not included in this count:
  //                      ChunkID and ChunkSize.

  const bitsPerSample = 16;
  const numberOfChannels = audioBuffer.numberOfChannels;
  const sampleRate = audioBuffer.sampleRate;
  let sampleCount = audioBuffer.length;

  if(duration > 0) {
    sampleCount = duration * audioBuffer.sampleRate;
  }

  const ch = numberOfChannels; // 1;
  const fmt = 1; //pcm
  const srate = sampleRate;
  // const bitsPerSample = sampleSize; // 16;
  const byteRate = srate * ch * bitsPerSample/8; // == SampleRate * NumChannels * BitsPerSample/8
  const blockAlign = ch * bitsPerSample/8; // == NumChannels * BitsPerSample/8 ; The number of bytes for one sample including all channels.
  // if(!sampleCount) {
  //   sampleCount = data.length / (bitsPerSample/8) / numberOfChannels;
  // }
  // I wonder what happens when this number isn't an integer?
  const subchunk1Size = 16; // 16 for PCM.  This is the size of the rest of the Subchunk which follows this number.
  const subchunk2Size = sampleCount * ch * bitsPerSample/8; // == NumSamples * NumChannels * BitsPerSample/8
  const chunkSize = 4 + 8 + subchunk1Size + 8 + subchunk2Size; // 4 + (8 + SubChunk1Size) + (8 + SubChunk2Size)
  // console.log(data.length, sampleCount, sampleCount*2)
  // const dblob = new Blob(data);
  const hblob = new Blob([
    "RIFF", u32ToArray(chunkSize), "WAVE",
    "fmt ", u32ToArray(subchunk1Size),
    u16ToArray(fmt), u16ToArray(ch), u32ToArray(srate),
    u32ToArray(byteRate),
    u16ToArray(blockAlign),
    u16ToArray(bitsPerSample),
    "data", u32ToArray(subchunk2Size),
    ], { type: '' });

  const data = new Int16Array(sampleCount * numberOfChannels);  // only if bits per sample = 16

  // const channels = [];
  // for(let i = 0; i < numberOfChannels; i++) {
  //   channels.push(audioBuffer.getChannelData(i));
  // }
  //
  // for(let i = 0; i < sampleCount; i++) {
  //   for(let j = 0; j < numberOfChannels; j++) {
  //     data[i * numberOfChannels + j] = channels[j][i] * 0x8000;
  //   }
  // }

  // function convertFloat32ToInt16(float32Array) {
  //   let int16Array = new Int16Array(float32Array.length);
  //   for (let i = 0; i < float32Array.length; i++) {
  //       // Scale the float to the range of an int16
  //       int16Array[i] = Math.max(-1, Math.min(1, float32Call[i])) * 32767;
  //   }
  //   return int16Array;
  // }

  let offset = 0;
  for (const arr of audioData) {
    for (let i = 0; i < arr.length; i++) {
      // Scale the float to the range of an int16
      data[offset] = Math.max(-1, Math.min(1, arr[i])) * 0x7fff /*32767*/;
      offset++;
    }
  }

  console.log('got data', data);

  const dblob = new Blob([data]);

  const blob = new Blob([hblob, dblob], { type: 'audio/wav' })

  return blob;
}


export function sliceAudioBuffer(audioCtx, originalBuffer, startSample, endSample) {
  const numChannels = originalBuffer.numberOfChannels;
  const sampleRate = originalBuffer.sampleRate;
  const length = endSample - startSample;
  const slicedBuffer = audioCtx.createBuffer(numChannels, length, sampleRate);

  for (let channel = 0; channel < numChannels; channel++) {
    const originalData = originalBuffer.getChannelData(channel);
    const sliceData = slicedBuffer.getChannelData(channel);
    // Using set() method of Float32Array for potentially faster copying
    sliceData.set(originalData.subarray(startSample, endSample));
  }

  return slicedBuffer;
}

export async function extractAudio(file, sampleRate, numberOfChannels, duration, cutoffRange) {
  try {
    return await extractAudio_WebAudioAPI(file, sampleRate, numberOfChannels, duration, cutoffRange);
  } catch (e) {
    return await extractAudio_FFmpeg(file, sampleRate, numberOfChannels, duration, cutoffRange);
  }
}

async function extractAudio_WebAudioAPI(file, sampleRate, numberOfChannels, duration, cutoffRange) {

  if (cutoffRange === undefined) {
    cutoffRange = 20; // last 20s
  }

  if (vad?.NonRealTimeVAD === undefined) {
    cutoffRange = 0;
  }

  if(!file) {
    return;
  }

  const audioCtx = new AudioContext();

  audioCtx.suspend();

  console.log('input file', file)
  const audioBuffer = await audioCtx.decodeAudioData(await file.arrayBuffer());
  console.log('audio buffer', audioBuffer)

  if(!sampleRate) {
    sampleRate = audioBuffer.sampleRate;
  }

  if(!numberOfChannels) {
    numberOfChannels = audioBuffer.numberOfChannels;
  }
  console.log(sampleRate, numberOfChannels)

  let bufferLength = sampleRate * audioBuffer.duration;

  if (duration !== undefined && duration > 0) {
    bufferLength = Math.min((duration + cutoffRange - 5 /* 5s overlap */) * sampleRate, bufferLength);
    if (duration * sampleRate >= bufferLength) {
      cutoffRange = 0;
    }
  } else {
    cutoffRange = 0;
  }

  const offlineAudioCtx = new OfflineAudioContext(numberOfChannels, bufferLength, sampleRate);

  const trackSource = offlineAudioCtx.createBufferSource();
  trackSource.buffer = audioBuffer;
  trackSource.connect(offlineAudioCtx.destination);
  trackSource.start(0);

  const renderedBuffer = await offlineAudioCtx.startRendering();

  let outputBuffer = renderedBuffer;

  // determine cutoff position using VAD

  try {
    if (cutoffRange > 0) {

      const startScan = bufferLength - Math.min(bufferLength, sampleRate * cutoffRange);

      let scanBuffer;
      if (startScan == 0) {
        scanBuffer = renderedBuffer;
      } else {
        scanBuffer = sliceAudioBuffer(audioCtx, renderedBuffer, startScan, bufferLength);
      }

      const options/* : Partial<vad.NonRealTimeVADOptions>*/ = {
        // https://docs.vad.ricky0123.com/user-guide/algorithm/
        // test: https://test.vad.ricky0123.com
        // baseAssetPath: "/lib/@ricky0123/vad-web/dist/",
        // onnxWASMBasePath: "/lib/onnxruntime-web/dist/",
        baseAssetPath: "/lib/vad2/",
        onnxWASMBasePath: "/lib/vad2/",

        model: "v5",
        frameSamples: 512,  // do not changes for v5; for the new, Silero version 5 model, it should be 512
        positiveSpeechThreshold: 0.5, // determines the threshold over which a probability is considered to indicate the presence of speech. default: 0.5
        minSpeechFrames: 5, // minimum number of speech-positive frames for a speech segment. default: 3
        preSpeechPadFrames: 10, // number of audio frames to prepend to a speech segment. default: 1
        negativeSpeechThreshold: 0.35, // determines the threshold under which a probability is considered to indicate the absence of speech. default: 0.35
        redemptionFrames: 8, // number of speech-negative frames to wait before ending a speech segment. default: 8

        // submitUserSpeechOnPause: false,
        // starOnLoad: true,
        // userSpeakingThreshold: 0.6,

        // values for model v4
        // positiveSpeechThreshold: 0.8,
        // minSpeechFrames: 5,
        // preSpeechPadFrames: 10,
        /** Threshold under which values returned by the Silero VAD model will be considered as indicating an absence of speech.
         * Note that the creators of the Silero VAD have historically set this number at 0.15 less than `positiveSpeechThreshold`.
         */
        // negativeSpeechThreshold: 0.6,
        /** After a VAD value under the `negativeSpeechThreshold` is observed, the algorithm will wait `redemptionFrames` frames
         * before running `onSpeechEnd`. If the model returns a value over `positiveSpeechThreshold` during this grace period, then
         * the algorithm will consider the previously-detected "speech end" as having been a false negative.
         */
        // redemptionFrames: 2,
      }

      const myvad = await vad.NonRealTimeVAD.new(options)
      const audioFileData = scanBuffer.getChannelData(0);
      // console.log('rendered ', renderedBuffer)
      // console.log('scan ', scanBuffer)
      // console.log('vad input', audioFileData)

      // let lastEnd = bufferLength;
      // let lastStart = bufferLength;
      let cutoff = bufferLength;

      for await (const {audio, start, end} of myvad.run(audioFileData, sampleRate)) {
        // do stuff with
        //   audio (float32array of audio)
        //   start (milliseconds into audio where speech starts)
        //   end (milliseconds into audio where speech ends)
        // console.log('vad start:', start, 'end:', end, 'audio:', audio);
        // console.log('vad start:', start, 'end:', end, 'start scan:', startScan, 'len:', bufferLength);
        console.log(`vad start: ${startScan / 16000 * 1000 + start} ms, end: ${startScan / 16000 * 1000 + end} ms, duration: ${duration * 1000} ms`);
        // lastEnd = startScan + end * 16 [# ms to sample #];
        // lastStart = startScan + start * 16 [# ms to sample #];
        if (startScan / 16 + start >= duration * 1000) {
          cutoff = startScan + start * 16 /* ms to sample */;
          break;
        } else if (startScan / 16 + end >= duration * 1000) {
          cutoff = startScan + end * 16 /* ms to sample */;
          break;
        }
      }

      // let cutoff = lastEnd;
      // if (lastEnd >= bufferLength)
      //   cutoff = lastStart;

      console.log(`cutoff position: ${cutoff / 16000 * 1000} ms, buffer length: ${bufferLength / 16000 * 1000} ms`);

      if (cutoff < bufferLength) {
        // console.log(`slicing ${cutoff} < ${bufferLength}`)
        outputBuffer = sliceAudioBuffer(audioCtx, renderedBuffer, 0, cutoff);
      } else {
        outputBuffer = renderedBuffer;
      }
    }
  } catch(e) {
    console.log(e)
    console.log(`VAD failed: ${e.message}`);
  }

  const blob = buffer2wav(outputBuffer);

  // const url = URL.createObjectURL(blob);

  return blob;
}

export class AudioRecorderVAD {
  constructor() {
    this.myvad = null;
    this.audioChunks = [];
    this.startTime = null;
    this.interval = null;
    this.timeString = '';
    this.time = 0;
    this.offset = 0;
    // this.stream = null;
    this.callbacks = {};
  }

  on(event, callback) {
    this.callbacks[event] = callback;
  }

  async startStopRecording() {
    if (this.isRecording) {
      this.stopRecording();
    } else {
      await this.startRecording();
    }
    return this.isRecording;
  }

  get isRecording() {
    return this.myvad?.listening;
  }

  stopRecording() {
    this.myvad?.pause();
    if (this.interval) {
      clearInterval(this.interval);
      this.interval = null;
    }
    const stream = this.myvad?.stream;
    this.myvad?.destroy();  // TODO: will this stop stream tracks?
    // if (stream) {
    //   stream.getTracks().forEach(track => track.stop());
    //   stream = null;
    // }
    // const audioBlob = new Blob(this.audioChunks, { type: 'audio/wav' });
    const audioBlob = this.audioChunks.length > 0 ? arrays2wav(this.audioChunks) : null;
    // // const audioUrl = URL.createObjectURL(audioBlob);
    if (this.callbacks.stop) {
      this.callbacks.stop(audioBlob);
    }
  }

  async startRecording(options) {
    this.audioChunks = [];
    this.time = 0;
    this.offset = 0;
    this.myvad = await vad.MicVAD.new({
      // baseAssetPath: "/lib/@ricky0123/vad-web/dist/",
      // onnxWASMBasePath: "/lib/onnxruntime-web/dist/",
      baseAssetPath: "/lib/vad2/",
      onnxWASMBasePath: "/lib/vad2/",

      model: "v5",
      frameSamples: 512,  // do not changes for v5; for the new, Silero version 5 model, it should be 512
      positiveSpeechThreshold: 0.5, // determines the threshold over which a probability is considered to indicate the presence of speech. default: 0.5
      minSpeechFrames: 5, // minimum number of speech-positive frames for a speech segment. default: 3
      preSpeechPadFrames: 10, // number of audio frames to prepend to a speech segment. default: 1
      negativeSpeechThreshold: 0.35, // determines the threshold under which a probability is considered to indicate the absence of speech. default: 0.35
      redemptionFrames: 8, // number of speech-negative frames to wait before ending a speech segment. default: 8

      onSpeechStart: () => {
        console.log("Speech start detected")
        // this.startTime = Date.now();
        this.updateTimer();
        // this.interval = setInterval(this.updateTimer.bind(this), 1000);
        if (this.callbacks.voicestart) {
            this.callbacks.voicestart();
        }
      },
      onSpeechEnd: (audio) => {
        // do something with `audio` (Float32Array of audio samples at sample rate 16000)...
        if (this.interval) {
          clearInterval(this.interval);
          this.interval = null;
        }
        const oldOffset = this.offset;
        this.offset += audio.length / 16000 * 100;
        this.time = this.offset * 10;
        this.updateTimer();
        this.startTime = null;
        this.audioChunks.push(audio);
        // TODO: make blobs for both?
        if (this.callbacks.voiceend) {
            this.callbacks.voiceend(array2wav(audio), oldOffset);
        }
      }
    })
    this.myvad.start();
    if (this.callbacks.start) {
      this.callbacks.start();
    }
    this.updateTimer();
  }

  updateTimer() {
    if (this.startTime !== null) {
      this.time += Date.now() - this.startTime;
    }
    const elapsedTime = this.time;
    const totalSeconds = Math.floor(elapsedTime / 1000);
    const hours = String(Math.floor(totalSeconds / 3600)).padStart(2, '0');
    const minutes = String(Math.floor((totalSeconds % 3600) / 60)).padStart(2, '0');
    const seconds = String(totalSeconds % 60).padStart(2, '0');
    const milliseconds = String(Math.floor(elapsedTime % 1000)).padStart(3, '0');

    // this.timeString = `${hours}:${minutes}:${seconds}.${milliseconds}`;
    this.timeString = `${hours}:${minutes}:${seconds}`;

    if (this.callbacks.timeupdate) {
      this.callbacks.timeupdate(elapsedTime, this.timeString);
    }
  }
}

export class AudioRecorder {
  constructor() {
    this.mediaRecorder = null;
    this.audioChunks = [];
    this.startTime = null;
    this.interval = null;
    this.timeString = '';
    this.time = 0;
    this.stream = null;
    this.callbacks = {};
  }

  on(event, callback) {
    this.callbacks[event] = callback;
  }

  async startStopRecording() {
    if (this.isRecording) {
      this.stopRecording();
    } else {
      await this.startRecording();
    }
    return this.isRecording;
  }

  get isRecording() {
    return this.mediaRecorder?.state === 'recording';
  }

  stopRecording() {
    // if (this.mediaRecorder) {
      this.mediaRecorder?.stop();
    // }
    if (this.stream) {
      this.stream.getTracks().forEach(track => track.stop());
      this.stream = null;
    }
  }

  async startRecording(options) {
    try {
      this.timeString = '';
      this.time = 0;
      this.stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      // mimeType = mimeType ?? 'audio/wav';
      // this.mediaRecorder = new MediaRecorder(stream, { mimeType });
      this.mediaRecorder = new MediaRecorder(this.stream, options);

      this.mediaRecorder.onstart = () => {
        this.audioChunks = [];
        this.startTime = Date.now();
        this.updateTimer();
        this.interval = setInterval(this.updateTimer.bind(this), 1000);
        if (this.callbacks.start) {
          this.callbacks.start();
        }
      };

      this.mediaRecorder.ondataavailable = event => {
        this.audioChunks.push(event.data);
      };

      this.mediaRecorder.onstop = () => {
        clearInterval(this.interval);
        this.updateTimer();
        const audioBlob = new Blob(this.audioChunks, { type: 'audio/wav' });
        // const audioUrl = URL.createObjectURL(audioBlob);
        if (this.callbacks.stop) {
          this.callbacks.stop(audioBlob);
        }
      };

      this.mediaRecorder.start();
    } catch (error) {
      console.error('Error accessing the microphone:', error);
    }
  }

  updateTimer() {
    const elapsedTime = Date.now() - this.startTime;
    const totalSeconds = Math.floor(elapsedTime / 1000);
    const hours = String(Math.floor(totalSeconds / 3600)).padStart(2, '0');
    const minutes = String(Math.floor((totalSeconds % 3600) / 60)).padStart(2, '0');
    const seconds = String(totalSeconds % 60).padStart(2, '0');
    const milliseconds = String(Math.floor(elapsedTime % 1000)).padStart(3, '0');

    this.time = elapsedTime;
    // this.timeString = `${hours}:${minutes}:${seconds}.${milliseconds}`;
    this.timeString = `${hours}:${minutes}:${seconds}`;

    if (this.callbacks.timeupdate) {
      this.callbacks.timeupdate(elapsedTime, this.timeString);
    }
  }
}

export function debounce(func, wait, immediate) {
  let timeout;
  let result;
  const timeouts = new Map();

  function debounced(...args) {
    const context = this;

    const later = function() {
      timeout = null;
      if (!immediate) {
        result = func.apply(context, args);
      }
    };

    const callNow = immediate && !timeout;
    clearTimeout(timeout);
    timeout = setTimeout(later, wait);

    if (callNow) {
      result = func.apply(context, args);
    }

    return result;
  }

  function cancel() {
    clearTimeout(timeout);
    timeout = null;
  }

  function immediateFn(...args) {
    const context = this;

    clearTimeout(timeout);
    timeout = null;

    result = func.apply(context, args);
    return result;
  }

  function cancelAll() {
    for (const timeoutsForKey of timeouts.values()) {
      clearTimeout(timeoutsForKey);
    }
    timeouts.clear();
  }

  function select(...keys) {
    const keysString = keys.join(',');
    let timeoutId = timeouts.get(keysString);

    if (timeoutId) {
      clearTimeout(timeoutId);
    }

    const debouncedForKey = function(...args) {
      const context = this;

      const later = function() {
        timeouts.delete(keysString);
        if (!immediate) {
          result = func.apply(context, args);
        }
      };

      const callNow = immediate && !timeouts.has(keysString);
      timeoutId = setTimeout(later, wait);
      timeouts.set(keysString, timeoutId);

      if (callNow) {
        result = func.apply(context, args);
      }

      return result;
    };

    const cancelForKey = function() {
      clearTimeout(timeoutId);
      timeouts.delete(keysString);
    };

    const immediateForKey = function(...args) {
      const context = this;

      clearTimeout(timeoutId);
      timeouts.delete(keysString);

      result = func.apply(context, args);
      return result;
    };

    const clearAllForKey = function() {
      clearTimeout(timeoutId);
      timeouts.delete(keysString);
    };

    debouncedForKey.cancel = cancelForKey;
    debouncedForKey.immediate = immediateForKey;
    debouncedForKey.clearAll = clearAllForKey;

    return debouncedForKey;
  }

  debounced.cancel = cancel;
  debounced.immediate = immediateFn;
  debounced.cancelAll = cancelAll;
  debounced.select = select;

  return debounced;
}

export function throttled(func, delay) {
    let lastExecutedTime = 0;

    return function(...args) {
        const now = Date.now();

        if (now - lastExecutedTime >= delay) {
            lastExecutedTime = now;
            func.apply(this, args);
        }
    };
}

export function generateRandomId(length) {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    let result = '';
    for (let i = 0; i < length; i++) {
        const randomIndex = Math.floor(Math.random() * chars.length);
        result += chars[randomIndex];
    }
    return result;
}

export async function decodeAudioData(audioBlob, sampleRate = 16000, numberOfChannels = 1) {

    const audioCtx = new AudioContext();

    audioCtx.suspend();

    const audioBuffer = await audioCtx.decodeAudioData(await audioBlob.arrayBuffer());

    if(!sampleRate) {
      sampleRate = audioBuffer.sampleRate;
    }

    if(!numberOfChannels) {
      numberOfChannels = audioBuffer.numberOfChannels;
    }

    const offlineAudioCtx = new OfflineAudioContext(numberOfChannels, sampleRate * audioBuffer.duration, sampleRate);

    const trackSource = offlineAudioCtx.createBufferSource();
    trackSource.buffer = audioBuffer;
    trackSource.connect(offlineAudioCtx.destination);
    trackSource.start(0);

    const renderedBuffer = await offlineAudioCtx.startRendering();

    return renderedBuffer;

    // const blob = buffer2wav(renderedBuffer);

    // const url = URL.createObjectURL(blob);

    // return blob;
}

// export async function decodeAudioData(audioBlob) {
//     const arrayBuffer = await audioBlob.arrayBuffer();
//     const audioContext = new (window.AudioContext || window.webkitAudioContext)();
//     return await audioContext.decodeAudioData(arrayBuffer);
// }

export function concatenateAudioBuffers(audioContext, audioBuffer1, audioBuffer2) {
    const numberOfChannels = Math.min(audioBuffer1.numberOfChannels, audioBuffer2.numberOfChannels);
    const length = audioBuffer1.length + audioBuffer2.length;
    const sampleRate = audioBuffer1.sampleRate;

    const outputBuffer = audioContext.createBuffer(numberOfChannels, length, sampleRate);

    for (let channel = 0; channel < numberOfChannels; channel++) {
        const channelData = outputBuffer.getChannelData(channel);
        channelData.set(audioBuffer1.getChannelData(channel), 0);
        channelData.set(audioBuffer2.getChannelData(channel), audioBuffer1.length);
    }

    return outputBuffer;
}

export async function concatenateAudioBlobs(blob1, blob2) {
    const audioContext = new (window.AudioContext || window.webkitAudioContext)();

    // Decode the audio blobs
    const [audioBuffer1, audioBuffer2] = await Promise.all([
        decodeAudioData(blob1),
        decodeAudioData(blob2)
    ]);

    // Concatenate the audio buffers
    const concatenatedBuffer = concatenateAudioBuffers(audioContext, audioBuffer1, audioBuffer2);

    // Encode the concatenated buffer to a WAV Blob
    return await encodeAudioBufferToWav(concatenatedBuffer);
}

// Usage example
// const concatenatedBlob = await concatenateAudioBlobs(blob1, blob2);

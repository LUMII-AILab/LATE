import WaveSurfer from 'wavesurfer.js';


let wavesurfer;

export function setWaveformTime(time) {
  // console.log('set waveform time to ', time)
  wavesurfer.setTime(time);
}

export function setTime(time) {
  return wavesurfer.setTime(time);
}

export function isPlaying() {
  return wavesurfer.isPlaying();
}

export function loadBlob(audio) {
  return wavesurfer.loadBlob(audio);
}

export function getDecodedData() {
  return wavesurfer.getDecodedData();
}

export function getDuration() {
  return wavesurfer.getDuration();
}

export function play() {
  wavesurfer.play();
}

export function pause() {
  wavesurfer.pause();
}

export function playPause() {
  wavesurfer.playPause();
}

export function zoom(minPxPerSec) {
  wavesurfer.zoom(minPxPerSec);
}

export function setPlaybackRate(value, preservePitch = true) {
  wavesurfer.setPlaybackRate(value, preservePitch);
}

export function on(name, listener) {
  return wavesurfer.on(name, listener);
}

export async function setupWaveform(settings) {
  const { container } = settings;

  wavesurfer = WaveSurfer.create({
    container: container,
    waveColor: '#4F4A85',
    progressColor: '#383351',
    // url: '/test.wav',
    cursorColor: 'red',
    cursorWidth: 2,
    height: 64,
    dragToSeek: true,
    fillParent: true,
    autoCenter: true,
    autoScroll: true,
    sampleRate: 16000,  // this is important for getDecodedData()
  })

  return {
    wavesurfer,
    zoom,
    play,
    playPause,
    pause,
    setPlaybackRate,
    getDuration,
    isPlaying,
    on,
    loadBlob,
    getDecodedData,
    setTime,
  };
}

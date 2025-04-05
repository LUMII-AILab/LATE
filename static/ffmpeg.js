
let ffmpeg, fetchFile;
let extractAudioNextID = 0;


async function initFFmpeg() {
  if (!ffmpeg) {
    try {
      const ffmpegCore = await import('@ffmpeg/core');
      const createFFmpeg = ffmpegCore.default;
      const util = await import('@ffmpeg/util');
      fetchFile = util.fetchFile;
      ffmpeg = await createFFmpeg({ log: true });
      console.debug('ffmpeg:', ffmpeg.FS);
    } catch (e) {
      console.error('error loading ffmpeg:', e);
      throw e;
    }
  }
}

function ffmpegVFSstat(path) {
  try {
    return ffmpeg.FS.stat(path);
  } catch (e) {
    return;
  }
}

export async function extractAudio(file, sampleRate = 16000, numberOfChannels = 1, duration = undefined, cutoffRange = undefined) {
  // file.type === 'video/quicktime'
  // file.name

  const extPos = file.name.lastIndexOf('.');
  let ext;
  if (extPos >= 1) {
    ext = file.name.substr(extPos);
  }

  if (!ext || ext.length === 0) {
    return;
  }

  if (!ffmpeg) {
    await initFFmpeg();
  }

  const id = extractAudioNextID++;

  const inputFile = `input-${id}${ext}`;
  const outputFile = `output-${id}.wav`;

  let data;

  try {
    const x = ffmpeg.FS.writeFile(inputFile, await fetchFile(file));
    // ffmpeg -i "$input" -ar 16000 -ac 1 -c:a pcm_s16le "$@"
    await ffmpeg.exec('-i', inputFile, '-ar', sampleRate.toString(), '-ac', numberOfChannels.toString(), '-c:a', 'pcm_s16le', outputFile);
    data = ffmpeg.FS.readFile(outputFile);
  } finally {
    if (ffmpegVFSstat(inputFile)) {
      ffmpeg.FS.unlink(inputFile);
    }
    if (ffmpegVFSstat(outputFile)) {
      ffmpeg.FS.unlink(outputFile);
    }
  }

  const audioBlob = new Blob([data.buffer], { type: 'audio/wav' });
  // const audioUrl = URL.createObjectURL(audioBlob);

  return audioBlob;
}

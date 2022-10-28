# ipcam264convert

Simple tool to convert surveillance cameras ".264" files into any a/v format supported by LibAV/FFMpeg.

```
Usage: ipcam264convert [-n] [-f format_name] [-q] input.264 [output.fmt]
  -n              Ignore audio data
  -f format_name  Force output format to format_name (ex: -f matroska)
  -q              Quiet output. Only print errors.
  -y              Overwrite output file if it exists.
  input.264       Input video file as produced by camera
  output.fmt      Output file. Format is guessed by extension (ex: output.mkv
                  will produce a Matroska file). If no output file is specified
                  one will be generated based on input file and the default
                  extension associated with the format provided through -f.
                  Note that you have to provide at least a valid output file
                  extension or a format name through -f option.

Available output formats and codecs depend on system LibAV/FFMpeg libraries.
```

This tool doesn't perform any transcoding: the original audio and video data is copied directly to the output container
streams. This work has been inspired by Ralph Spitzner reverse engineering of his KKMoon camera output files 
(https://spitzner.org/kkmoon.html). If you like this tool, please consider donating to Ralph via the "Donate" button 
available on his page.

### Supported cameras

Probably many, however it's difficult to make a comprehensive list. There is a good chance that if you own a cheap
chinese IP camera that supports motion detection, H.264 and saves videos on an SD-Card you will be able to convert your
`.264` videos using this tool. The expected input format is the one described in https://spitzner.org/kkmoon.html which 
is essentially a sequence of custom format packets, each carrying either a video frame or some audio samples.

The tool currently assumes the video codec is `H.264` and the audio codec is `a-law`. Video size, frame rate and audio 
sampling frequency are guessed from the input but the audio is assumed to be `mono`. 

There are two videos included under `test_videos` which are `1920x1080, H.264 @ 12FPS, a-law mono @ 8000Hz`. 
If you happen to have a `.264` video which doesn't work with this tool, feel free to share it with me.

### Building

A recent version of libav/FFmpeg is required, along with cmake, pkg-config and gcc and obviously git. 
For instance, under Debian you can just

```commandline
apt install gcc cmake pkg-config libavformat-dev libavcodec-dev libavutil-dev git
```

Depending on your distro, you might want to check out [http://www.deb-multimedia.org/](http://www.deb-multimedia.org/) 
first. Then

```commandline
git clone https://github.com/francescovannini/ipcam264convert.git
cd ipcam264convert
mkdir build
cd build
cmake ..
make
./ipcam264convert
```

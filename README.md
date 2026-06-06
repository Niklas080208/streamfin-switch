## Streamfin is a background music player built for Ultrahand Overlay that lets you stream music from your Jellyfin server! 

Heavily built off of the wonderful work of [sys-tune](https://github.com/HookedBehemoth/sys-tune), I replaced the SD audio card source with a streaming Jellyfin HTTP source. Supports FLACs, WAVs, and MP3's streaming directly from your Jellyfin library!

## Installation Instructions

[Download Here
](https://github.com/dammitjeff/streamfin-switch/releases/latest)
- Drag and drop the two folders to the root of your SD card
- Reboot the console
- Open Ultrahand Overlay (default L + DPad-Down + R-stick)
- Open Streamfin and scroll down to Settings
- Enter your server (host:port) and pair with Quick Connect

**Note for Mac Users**: By default, Mac tends to hide .overlay folders. When transferring over the files from a Mac, be sure to do CMD + Shift + . to show hidden files, to ensure the entire folder gets transferred over to your switch. 

## Special thanks
- [HookedBehemoth](https://github.com/HookedBehemoth/sys-tune) for **sys-tune**, Streamfin's player is heavily built off of.
- [David Reid (mackron)](https://github.com/mackron) for **[dr_libs](https://github.com/mackron/dr_libs/)** — the `dr_flac` / `dr_mp3` / `dr_wav` audio decoders.
- [Serge Zaitsev](https://github.com/zserge) for **[jsmn](https://github.com/zserge/jsmn)**, which parses JSON used for the Jellyfin API.
- The **[libjpeg-turbo](https://libjpeg-turbo.org/)** decodes cover art jpegs.

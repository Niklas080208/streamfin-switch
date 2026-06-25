## Streamfin is a music player for Ultrahand Overlay that lets you stream music from your Jellyfin server! 
<p align="center">
  <img width="800" alt="Still 2026-06-06 135124_1 3 1-2" src="https://github.com/user-attachments/assets/168cfa45-ffde-4710-9da6-5d91a30e59ad" />
</p>

Heavily built off of the wonderful work of [sys-tune](https://github.com/HookedBehemoth/sys-tune), Streamfin replaces the SD audio card source with a streaming Jellyfin HTTP source. Supports FLACs, WAVs, and MP3's streaming directly from your Jellyfin library!

**REQUIRES [ULTRAHAND OVERLAY](https://github.com/ppkantorski/Ultrahand-Overlay) TO BE INSTALLED!!** 
Go install that first from the link, or you can one-click install it in the homebrew app store of your choice.

## Need help, want updates, or want to show off your setup?
<p align="center">
  <a href="https://discord.gg/mQxBxGgT5h"><img src="https://img.shields.io/badge/Discord-Join%20the%20community-5865F2?logo=discord&logoColor=white&style=for-the-badge" alt="Join the Streamfin Discord"></a>
</p>

> [!WARNING]
> Due to the complexity of streaming over the internet, Streamfin uses a decent amount of memory, and the Switch only gives homebrew a small shared pool. If playback won't start, you likely have too many overlays/sysmodules installed — try removing a few.

## Installation Instructions

## [BIG DOWNLOAD BUTTON HERE](https://github.com/dammitjeff/streamfin-switch/releases/latest)
- Drag and drop the two folders to the root of your SD card
- Reboot the console
- Open Ultrahand Overlay (default L + DPad-Down + R-stick)
- Open Streamfin and scroll down to Settings
- Enter your server (host:port) and pair with Quick Connect

**Note for Mac users**: By default, Mac tends to hide .overlay folders. When transferring over the files from a Mac, be sure to do CMD + Shift + . to show hidden files, and make sure the `.overlays` folder ACTUALLY gets transferred over to your switch. 

## Screenshots!
<img width="1280" height="720" alt="2026060613081500-334BB3B1BC68FB8B9F441C8EAE4F8F4E" src="https://github.com/user-attachments/assets/2f770320-ebd6-4bad-8424-a2ecea2464e3" />
<br>
<br>
<p align="center">
<img height="480" alt="2026060614123700-C7C3CAEB6C50DA2C777325EA990171EE" src="https://github.com/user-attachments/assets/85df0597-f0b0-4044-8724-a5d862c35f8b" />
<br> Sign in via Jellyfin Quick Connect makes it much easier to connect to your server!
</p>

<p align="center">
<img  height="480" alt="2026060614120200-C7C3CAEB6C50DA2C777325EA990171EE" src="https://github.com/user-attachments/assets/359c549e-9505-45da-b747-a9065f2057a9" />
<br> Browse through all of your artists, or hit Y to create a radio station based off the artist!
</p>
<p align="center">
<img height="480" alt="2026060614074700-6EEDD66FEF4A4CCC31CCF68D2CE2B6B5" src="https://github.com/user-attachments/assets/3b243f57-1f1d-4e1e-bc4f-deda55e5ed9b" />
<br> Music is persistant, even when switching between games. :)
</p> 


## Special thanks
- [HookedBehemoth](https://github.com/HookedBehemoth/sys-tune) for **sys-tune**, Streamfin's player is heavily built off of.
- [David Reid (mackron)](https://github.com/mackron) for **[dr_libs](https://github.com/mackron/dr_libs/)** — the `dr_flac` / `dr_mp3` / `dr_wav` audio decoders.
- [Serge Zaitsev](https://github.com/zserge) for **[jsmn](https://github.com/zserge/jsmn)**, which parses JSON used for the Jellyfin API.
- **[libjpeg-turbo](https://libjpeg-turbo.org/)** which decodes cover art jpegs.

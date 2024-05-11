# Music
Dotfile based music download and management system

## Build
Depends on the following:

- [TagLib](https://taglib.org/)
- [ncurses](https://invisible-island.net/ncurses/)
- [libmpdclient](https://www.musicpd.org/libs/libmpdclient/)
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) (optional, will be called as an external process)

```build.sh
$ ./build.sh
```

## Usage
Create a file named `.config` in your music directory with the following format. Execute the program in the same directory

```conf
# Comment
# Use tabs for indentation, NOT space

Artist 1
	Album 1
		@https://www.youtube.com/link1
		@https://www.youtube.com/link2
		...

		Song Name 1 @ path1
		Song Name 2 @ path2
		...

	Album 2
		...

Artist 2
	...
```

| Key | Description |
| --- | ----------- |
| <kbd>q</kbd> | Quit |
| <kbd>j</kbd> | Select next item in the current column |
| <kbd>k</kbd> | Select previous item in the current column |
| <kbd>l</kbd> | Select the next column |
| <kbd>h</kbd> | Select the previous column |
| <kbd>t</kbd> | Save tags for the current album/song |
| <kbd>Enter</kbd> | Play current album/song |
| <kbd>Space</kbd> | Play/Pause |
| <kbd>n</kbd> | Next song |
| <kbd>p</kbd> | Previous song |
| <kbd>,</kbd> | Go back 5 seconds |
| <kbd>.</kbd> | Go forward 5 seconds |

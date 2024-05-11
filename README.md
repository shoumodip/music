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

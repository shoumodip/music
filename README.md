# Music
Dotfile based music download and management system

## Build
- Calls [yt-dlp](https://github.com/yt-dlp/yt-dlp) as an external process, optional

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

## Keys
| Key | Description |
| --- | ----------- |
| <kbd>q</kbd> | Quit |
| <kbd>Space</kbd> | Play/Pause |
| <kbd>n</kbd> | Next song |
| <kbd>p</kbd> | Previous song |
| <kbd>,</kbd> | Go back 5 seconds |
| <kbd>.</kbd> | Go forward 5 seconds |

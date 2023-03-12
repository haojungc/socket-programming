# Web Server
A simple HTTP server that accepts HTTP1.1 GET requests.

## Features
### Get HTML Files
You can GET HTML files by typing in the URL address bar on your browser. For example, you can type `localhost:3333/HelloWorld.html` to access file `HelloWorld.html`.

## Usage
### Compilation
```bash
$ make
```

### Launching Server
```bash
$ ./server [-static <path-to-static-directory>]
```

## TODO
- [x] Support 200 code
- [x] Set static directory via command line
- [ ] Handle HTML files that are larger than socket buffer
- [ ] Implement HTTP client
- [ ] Support common content types
- [ ] Display PDF files on the browser
- [ ] Auto-refresh when source code is being updated (how to monitor code changes?)
- [ ] Switch from fork() to POSIX threads and benchmark the performance

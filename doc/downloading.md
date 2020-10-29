# Getting pre-compiled DG

You can obtain pre-compiled DG project in several forms.

## Docker image

You can download the image from [Docker Hub](https://hub.docker.com/r/mchalupa/dg). The image
contains, apart from DG, also vim and emacs editors and clang, so that you can try dg out.
(Note that this image is not being updated regularly).
Alternatively, the root folder of dg contains a Dockerfile which allows you to build
a docker image with dg installed. Just run these commands from the dg's root directory:

```
docker build . --tag dg:latest
docker run -ti dg:latest
```

Note that the locally built image does not contain either vim or emacs, so you must
install one of them if you need.

## Binary Packages

We have packed DG for several systems. The packages contain only the library and `llvm-slicer`. Other tools are not included.

### Ubuntu package

A binary package for Ubuntu 18.04 can be found in [Releases](https://github.com/mchalupa/dg/releases/tag/v0.9-pre).


### Archlinux AUR package

There is also the [dg-git](https://aur.archlinux.org/packages/dg-git/) AUR package for Archlinux users.

# uml_xattrcred
A tool to scan directory hierarchy and copy current mode and ownership into extended attributes for User-Mode Linux (UML) hostfs

A possible use case for this is to download a bootable rootfs image, unpack it on the host filesystem, and then save permissions and ownership that should be used by the UML in extended attributes.

After that, it is possible to change mode/ownership on host with regular chmod/chown (perhaps setting all files to be accessible by an unprivileged account the UML kernel runs on), while UML would continue working with old permissions/ownership from the copy in extended attributes.

Both the kernel hostfs patch and this tool are still in development, and this README will hopefully be updated in the future.

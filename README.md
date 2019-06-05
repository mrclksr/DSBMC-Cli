
# ABOUT

**dsbmc-cli**
is a command-line client for DSBMD that provides a simple interface
to query information about storage devices, and to send requests to
mount, unmount and eject these.

# INSTALLATION

	# git clone https://github.com/mrclksr/DSBMC-Cli.git
	# git clone https://github.com/mrclksr/libdsbmc.git
	# cd DSBMC-Cli && make install

# USAGE

**dsbmc-cli**
**-L** *event* *command*
\[*arg ...*]
&#59;
\[**-L** *...*]  
**dsbmc-cli**
**-a**
\[**-b** *dev1,dev2,...*]
\[**-U** *time*]
\[\[**-L** *event* *command* \[*arg ...*]]
&#59;
\[**-L** *...*]]  
**dsbmc-cli**
{{**-e** | **-u**} \[**-f**] | {**-m** | **-s** | **-u** | **-v** *speed*}}
*device*  
**dsbmc-cli**
{**-e** | **-u**}
*&lt;mount point&gt;*  
**dsbmc-cli**
**-i**
*&lt;disk image&gt;*  
**dsbmc-cli**
**-l**  
**dsbmc-cli**
\[**-h**]

# OPTIONS

**-L**

> Listen for
> *event*,
> and execute the
> *command*
> every time the
> *event*
> is received. Possible events are
> *mount*,
> *unmount*,
> *add*,
> and
> *remove*.
> The
> *command*
> must be terminated by a semicolon
> ('&#59;')
> as a separate argument. Each event provides information about the device
> that can be accessed using the placeholders
> *%d*
> (device name),
> *%l*
> (volume label),
> *%m*
> (mount point) (only
> *mount*
> and
> *unmount*
> ), and
> *%t*
> (device type). A literal percent character
> ('%')
> must be escaped by a further
> '%'
> character
> ("%%").
> Possible values for the device type
> (*%t*)
> are
> *hdd*, *usbdisk*, *datacd*, *audiocd*, *dvd*, *vcd*, *svcd*, *mmc*, *mtp*,
> and
> *ptp*.
> The
> **-L**
> option can be given multiple times.

**-U**

> Auto-unmount. Try to unmount each automounted device every
> '*time*'
> seconds.

**-a**

> Automount. After mounting all devices presented by DSBMD,
> dsbmc-cli(1)
> waits for new devices added to the system, and mounts them.
> In addition, the
> **-L**
> option can be specified for each event. If
> dsbmc-cli(1)
> automounts a device, it executes the command defined for the
> *mount*
> event.

**-b** *dev1,dev2,...*

> A comma-separated list of devices and/or volume labels to ignore if the
> **-a**
> option is given. Volume labels must be prefixed by
> "volid=".
> Example: dsbmc-cli -a -b da0p2,volid=EFISYS,volid=TMP.

**-e** *device*, **-e** *&lt;mount point&gt;*

> Eject the given
> *device*
> or the device mounted on
> *&lt;mount point&gt;*

**-i** *&lt;disk image&gt;*

> Create a memory disk to access the given
> *disk image*.

**-l**

> List all currently attached devices DSBMD supports.

**-m** *device*

> Mount the given
> *device*.

**-s** *device*

> The storage capacity of the given
> *device*
> is queried, and the media size, the number of used and free bytes are
> printed to stdout.

**-u** *device*, **-u** *&lt;mount point&gt;*

> Unmount the given
> *device*
> or the device mounted on
> *&lt;mount point&gt;*

**-v** *speed* *device*

> Set the reading speed of the given CD/DVD
> *device*.

# EXAMPLES

## Automounting

Just execute
'dsbmc-cli -a'
or, if you wish, add the command
'dsbmc-cli -a 2&gt;/dev/null&'
to your shell's startup file, your display
manager's startup file, or your
*~/.xsession*.
If you don't want to manually unmount your devices, you can in addition use
the
**-U**
flag.

## Events

	$ dsbmc-cli -L mount printf "%%s was mounted on %%s\n" %d %m ';'

This command listens for
*mount*
events, and executes the
*printf*
command, where %d and %m are replaced by the device name and mount point.

	$ dsbmc-cli -a -L add sh -c 'case %t in audiocd) vlc cdda://%d&;; dvd) vlc dvd://%d&;; esac' ';' -L mount sh -c 'Thunar %m&' ';'

In this example,
**dsbmc-cli**
automounts devices, and listens for
*add*
and
*mount*
events. If an audio CD or a DVD was inserted, the command
*vlc cdda://%d&*
or
*vlc dvd://%d&*
is executed, respectively. If a device was mounted,
**dsbmc-cli**
executes the command
*Thunar %m&*,
where %m is replaced by the mount point.


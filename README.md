
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

**-e** *device*, **-e** *&lt;mount point&gt;*

> Eject the given
> *device*
> or the device mounted on
> *&lt;mount point&gt;*

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

## 1 Automounting

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

## 1.1 Using dsbmc-cli together with fb-unmount

If you're using Fluxbox, you might be interested in using the
script
'*fb-unmount*',
which automatically adds entries of mounted devices to
your Fluxbox menu. This allows you to unmount and eject devices by clicking
the corresponding menu entry:

Get the script from
[https://github.com/mrclksr/fb-unmount](https://github.com/mrclksr/fb-unmount),
save it in
*~/bin*,
and make it executable (chmod u+x ~/bin/fb-unmount).
Finally add the following line(s) to your
*~/.fluxbox/startup*:

	dsbmc-cli -a -L mount fb-unmount -m %d %m ';' \
	    -L unmount fb-unmount -u %d ';'&
	


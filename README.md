<img src="packaging/io.github.supertoq.oledsaver.svg" height="128">

# Basti's OLED Saver
Basti's OLED Saver is a screensaver tool that prevents the device from entering standby, automatic suspend, screen blanking, and screen locking. It can display a pure black fullscreen to protect OLED displays from burn-in.


![oledsaver main window](data/img/oledsaver_preview_img1.png?raw=true) 
  
![oledsaver settings page](data/img/oledsaver_preview_img2.png?raw=true) 
  
It provides options to start directly in screensaver mode and to close the application using the spacebar, allowing quick start and exit via a keyboard shortcut.  
  
This app was created at the request of good buddy.  
  
  
## Installing  
The quickest way to get Basti's OLED Saver is to download the flatpak file from the [Releases](https://github.com/supertoq/OLED-Saver/releases) page.  
Installation proceeds as follows:  
```
cd ~/Downloads  
```  
```
flatpak install -y --user io.github.supertoq.oledsaver.flatpak
```  
  
You can also build the application yourself from the source code. One way to do this is using Flatpak Builder.
  
## Building with Flatpak Builder.  

### Dependencies:
  
#### Ubuntu/Debian  
```
sudo apt update && sudo apt install flatpak flatpak-builder
```  
  
#### Fedora  
```
sudo dnf upgrade && sudo dnf install flatpak flatpak-builder 
```  
  
#### Arch
```
sudo pacman -Syu && sudo pacman -S flatpak flatpak-builder 
```  

### Add Flathub Repository 
```
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo 
```  
  
### Install the GNOME SDK 49 
```
flatpak install org.gnome.Sdk/x86_64/49
```  
  
## Building
  
### Clone Repository:  
```
git clone https://github.com/supertoq/OLED-Saver.git 
```  
  
```
cd OLED-Saver 
```  
```
flatpak-builder --user --install --force-clean _build-dir io.github.supertoq.oledsaver.yml 
```  
  
### Running  
```
flatpak run io.github.supertoq.oledsaver 
```  
  
#### If you want to uninstall:  
```
flatpak uninstall -y io.github.supertoq.oledsaver 
```  
  
> [!Note]  
> Use of this code and running the application is at your own risk. I accept no liability.
  


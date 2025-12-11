## Basti's OLED-Saver
Basti's OLED-Saver is a small tool that tries to prevent the device from standby, automatic suspending, screen blanking and screen locking. You can display a black fullscreen to protect the OLED from burn-in.

![oledsaver](oledsaver_preview_img2.png?raw=true) 

This app was created at the request of good buddy.  
  
  
### Installation:  
The quickest way to install OLED‑Saver is to download the application from the [Releases](https://github.com/super-toq/OLED-Saver/releases) page.  
Installation proceeds as follows:  
```
cd ~/Downloads  
```  
```
flatpak install -y --user free.basti.oledsaver.flatpak  
```  
  
You can also build the application yourself from the transparent source code; here’s one way using Flatpak Builder.
  
### Building and Installing with Flatpak Builder.  

#### Preparation and Dev Depentencies:
  
#### * Ubuntu/Debian  
```
sudo apt update && sudo apt install flatpak flatpak-builder
```  
  
#### * Fedora  
```
sudo dnf install flatpak flatpak-builder 
```  
  
#### * Arch
```
sudo pacman -S flatpak flatpak-builder 
```  

##### Add Flathub repository: 
```
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo 
```  
  
#### Within Flatpak, the `Gnome SDK 49` is required to build the application: 
```
flatpak install org.gnome.Sdk/x86_64/49
```  
  
### Build and install OLED-Saver:
  
#### Clone repository:  
```
git clone https://github.com/super-toq/OLED-Saver.git 
```  
  
```
cd OLED-Saver 
```  
```
flatpak-builder --user --install --force-clean build-dir free.basti.oledsaver.yml 
```  
  
### Run the OLED-Saver:  
```
flatpak run free.basti.oledsaver 
```  
  
### Uninstall the application:  
```
flatpak uninstall -y free.basti.oledsaver 
```  
  
**Please note**:  
***This code is part of my learning project.  
Use of the code and execution of the application is at your own risk; 
I accept no liability!***
  


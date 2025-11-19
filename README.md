## Basti's OLED-Saver
Basti's OLED-Saver is a small tool that tries to prevent the device from standby.  
A black full screen is displayed to protect the OLED from burn-in.  

![oledsaver](oledsaver_preview_img1.png?raw=true) 

This app was created at the request of a user.  
  
  
### Installation:  
The quickest way to install OLED‑Saver is to download the application from the [Releases](https://github.com/super-toq/OLED-Saver/releases) page.  
Installation proceeds as follows:  
```
cd ~/Downloads  

flatpak install --user oledsaver.flatpak  
```  
  
You can also build the application yourself from the transparent source code; here’s one way using Flatpak Builder.
  
### Building and Installing with Flatpak Builder.  

#### Preparation and Dev Depentencies:
  
#### Ubuntu/Debian  
`sudo apt update && sudo apt install flatpak flatpak-builder`  
  
#### Fedora  
`sudo dnf install flatpak flatpak-builder`  
  
#### Arch
`sudo pacman -S flatpak flatpak-builder`  

##### Add Flathub repository (if not already added): 
`flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo`  
  
### Clone repository:  
`git clone https://github.com/super-toq/OLED-Saver.git`  
  
### Build and install OLED-Saver:
`cd OLED-Saver`  
`flatpak-builder --user --install --force-clean build-dir free.basti.oledsaver.yml`  
  
### Run the OLED-Saver:  
`flatpak run free.basti.oledsaver`  
  
### Uninstall the application:  
`flatpak uninstall -y free.basti.oledsaver`  

  
**Please note**:
***This code is part of my learning project.  
Use of the code and execution of the application is at your own risk; 
I accept no liability!***



# B.A.T.M.A.N 
## Better Approach to Mobile Ad-hoc Networking (non-official)
<p align="center"> 
     <a  href="https://hub.docker.com/r/abelkidane/ns3-dce" ><img alt="Static Badge" src="https://img.shields.io/badge/docker-abelkidane%2Freports-blue?logo=docker" target="_blank">    
</p>

Simple simulation code for simulating the batman protocol in ns-3.
(N.B. This repo contains a from-scratch Batman protocol implementation and is not recommended for use. Please use the official release instead.)

## Prerequisite 
Install Docker in your PC by following the installation instructions from [Docker](https://docs.docker.com/engine/install/)
For Windows, install the Docker Desktop Application. 
For Ubuntu, install it using the following commands.
```bash
sudo apt update
sudo apt install docker.io

# to verify installation just type
docker
```
To avoid usign sudo everytime we use docker, create a group and add the user to it
```bash
sudo groupadd docker
sudo usermod -aG docker $USER
# verify using 
docker run hello-world
```

## How to use
Clone this repo and pull the image for the project from Docker Hub.

```bash 
git clone https://github.com/AbelKidaneHaile/batman.git 
docker pull abelkidane/ns3-dce 
cd batman
```
Then, start the container by using docker compose.

```bash 
docker compose run ns3dce

OR alternatively (this removes warnings that docker compose has leftover from older runs)
docker compose run --rm --remove-orphans ns3dce
```
Now, we are ready to run ns3 terminal commands using waf. Make sure to configure at least once. Building might take a bit of time. 

```bash 
./waf configure --enable-examples --enable-tests
./waf build
```
Finally, we can run the simulation

```bash 
./waf --run "batman-simulation --nNodes=5 --duration=12"
```

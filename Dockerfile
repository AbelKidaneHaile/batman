FROM thehajime/ns-3-dce:latest
RUN sudo apt-get update
RUN sudo apt install nano
RUN mkdir -p /home/ns3dce/dce-linux-dev/source/ns-3-dev/src/batman
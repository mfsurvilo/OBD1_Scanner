#!/bin/bash
# Install Platformio
sudo apt update
sudo apt install -y python3-pip
pip3 install platformio


# Create Python virtual environment
python3 -m venv venv

# Activate it
source venv/bin/activate

# Install dependencies from requirements.txt
pip install -r requirements.txt

source venv/bin/activate

echo "Virtual environment created and activated."
echo "Run 'source venv/bin/activate' to activate in new shells."

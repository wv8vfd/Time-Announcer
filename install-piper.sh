#!/bin/bash
# Install Piper TTS to /opt/piper

set -e

PIPER_VERSION="v1.2.0"
INSTALL_DIR="/opt/piper"

echo "Installing Piper TTS to $INSTALL_DIR..."

# Create directory
sudo mkdir -p $INSTALL_DIR
cd /tmp

# Download piper
echo "Downloading Piper..."
wget -q https://github.com/rhasspy/piper/releases/download/${PIPER_VERSION}/piper_amd64.tar.gz
tar -xzf piper_amd64.tar.gz

# Move to install directory
sudo mv piper/* $INSTALL_DIR/
rm -rf piper piper_amd64.tar.gz

# Download a good voice (lessac - clear American English)
echo "Downloading voice model (en_US-lessac-medium)..."
cd $INSTALL_DIR
sudo wget -q https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx
sudo wget -q https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json

# Test it
echo ""
echo "Testing Piper..."
echo "Hello, this is a test of the Piper text to speech system." | $INSTALL_DIR/piper --model $INSTALL_DIR/en_US-lessac-medium.onnx --output_file /tmp/piper_test.wav 2>/dev/null

if [ -f /tmp/piper_test.wav ]; then
    echo "✓ Piper installed successfully!"
    echo ""
    echo "Installation directory: $INSTALL_DIR"
    echo "Voice model: $INSTALL_DIR/en_US-lessac-medium.onnx"
    echo ""
    echo "Test with: echo \"Hello world\" | $INSTALL_DIR/piper --model $INSTALL_DIR/en_US-lessac-medium.onnx --output_file test.wav"
    echo ""
    echo "Other voices available at: https://huggingface.co/rhasspy/piper-voices/tree/main"
else
    echo "✗ Installation may have failed - test file not created"
    exit 1
fi

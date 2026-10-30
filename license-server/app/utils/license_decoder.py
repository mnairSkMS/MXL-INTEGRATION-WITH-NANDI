import base64
import json
import logging
from pathlib import Path
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.asymmetric import ed25519
from cryptography.hazmat.primitives import serialization


# Configure logging
logging.basicConfig(level=logging.INFO)

# Constants for key paths
ENCRYPTION_KEY_PATH = Path(".") / 'certificates' / 'license_encryption_key.bin'
PUBLIC_KEY_PATH = Path(".") / 'certificates' / 'license_ed25519_public_key.der'

def read_encryption_key(key_path=ENCRYPTION_KEY_PATH):
    """Read the encryption key from the binary file."""
    with open(key_path, 'rb') as f:
        return f.read()

def read_public_key(key_path=PUBLIC_KEY_PATH):
    """Read the Ed25519 public key from the DER file."""
    with open(key_path, 'rb') as f:
        key_data = f.read()
        # Load the public key from DER format
        return serialization.load_der_public_key(key_data)

def verify_signature(public_key, message, signature):
    """Verify the Ed25519 signature of a message."""
    try:
        # The message is the base64-encoded encrypted data
        # The signature is already base64-decoded
        public_key.verify(signature, base64.b64decode(message))
        return True
    except Exception as e:
        logging.error(f"Signature verification failed: {str(e)}")
        return False

def decrypt_aes_gcm(encrypted_data_str, key):
    """Decrypt AES-256-GCM encrypted data."""
    try:
        # The data is base64-encoded twice
        # First decode: Get the inner base64 string
        inner_b64 = base64.b64decode(encrypted_data_str).decode('utf-8')
        # Second decode: Get the actual nonce + ciphertext
        
        # Extract nonce (12 bytes) and ciphertext
        ciphertext_b64, nonce_b64 = inner_b64.rsplit('.', 1)
        ciphertext = base64.b64decode(ciphertext_b64)
        nonce = base64.b64decode(nonce_b64)
        
        # Create AESGCM instance
        aesgcm = AESGCM(key)
        
        # Decrypt the data
        decrypted_data = aesgcm.decrypt(nonce, ciphertext, None)
        return decrypted_data
    except Exception as e:
        logging.error(f"Error decrypting data: {str(e)}")
        logging.error(f"Error type: {type(e)}")
        raise

def decode(license_data, key_path=ENCRYPTION_KEY_PATH):
    """
    Decode and decrypt a license data.
    
    Args:
        license_data (dict): A dictionary containing 'enc' and 'sig' fields
        key_path (str, optional): Path to the encryption key file. Defaults to ENCRYPTION_KEY_PATH.
        
    Returns:
        dict: The decrypted license data
        
    Raises:
        ValueError: If the license data is invalid or decryption fails
    """
    try:
        if not isinstance(license_data, dict) or 'enc' not in license_data or 'sig' not in license_data:
            raise ValueError("Invalid license data format: expected dict with 'enc' and 'sig' fields")
        
        # Verify signature
        public_key = read_public_key()
        signature = base64.b64decode(license_data['sig'])
        # The message to verify is the encrypted data
        if not verify_signature(public_key, license_data['enc'], signature):
            raise ValueError("License signature verification failed")
        
        # Read the encryption key
        encryption_key = read_encryption_key(key_path)
        
        # Decrypt the encrypted content
        decrypted_data = decrypt_aes_gcm(license_data['enc'], encryption_key)
        
        # Parse the decrypted JSON
        return json.loads(decrypted_data.decode('utf-8'))
        
    except Exception as e:
        logging.error(f"Failed to process license: {str(e)}")
        raise
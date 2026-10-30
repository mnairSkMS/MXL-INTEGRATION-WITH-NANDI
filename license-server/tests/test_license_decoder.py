import pytest
from pathlib import Path
import base64
import json
from app.utils.license_decoder import decode

@pytest.fixture
def license_data():
    """Fixture providing the parsed license data."""
    license_path = Path(__file__).parent / 'transcoder.lic'
    with open(license_path, 'r') as f:
        content = f.read()
        # Remove header and footer
        content = content.replace('-----BEGIN TRANSCODER LICENSE-----', '')
        content = content.replace('-----END TRANSCODER LICENSE-----', '')
        # Remove whitespace and newlines
        content = content.strip()
        # Decode the base64 content
        return json.loads(base64.b64decode(content).decode('utf-8'))

def test_decode_license(license_data):
    """Test decoding and decrypting a license with signature verification."""
    # Decode the license using default paths
    decrypted_data = decode(license_data)
    print(decrypted_data)
    # Verify the decrypted data structure
    assert isinstance(decrypted_data, dict)
    assert 'data' in decrypted_data
    assert 'meta' in decrypted_data
    
    # Verify data fields
    data = decrypted_data['data']
    assert 'id' in data
    assert 'name' in data
    assert 'valid_since' in data
    assert 'ttl' in data
    
    # Verify meta fields
    meta = decrypted_data['meta']
    assert 'created_at' in meta

def test_decode_invalid_format():
    """Test that decoding with invalid data format raises an error."""
    invalid_data = {"invalid": "format"}
    with pytest.raises(ValueError, match="Invalid license data format"):
        decode(invalid_data)

def test_decode_invalid_signature(license_data):
    """Test that decoding with an invalid signature raises an error."""
    # Modify the signature to make it invalid
    license_data['sig'] = 'invalid_signature'
    
    # Try to decode with signature verification
    with pytest.raises(ValueError, match="License signature verification failed"):
        decode(license_data) 
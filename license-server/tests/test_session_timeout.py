import pytest
import httpx
import time
import logging
import jwt
from cryptography.hazmat.primitives import serialization
import base64
import json

# Configure logging
logging.basicConfig(level=logging.INFO)

BASE_URL = "https://127.0.0.1:8000"

def get_httpx_client():
    """Create an httpx client configured to accept self-signed certificates"""
    return httpx.Client(verify=False)  # Disable SSL verification for self-signed certificates

def read_public_key():
    with open("/home/mkl/ws/proj/skandha/git/transcoder-license-server/certificates/license_ed25519_public_key.der", "rb") as f:
        key_data = f.read()
        return serialization.load_der_public_key(key_data)

def verify_token(token):
    try:
        public_key = read_public_key()
        decoded = jwt.decode(token, public_key, algorithms=["EdDSA"])
        return decoded
    except Exception as e:
        logging.error(f"Token verification failed: {str(e)}")
        return None

def read_license_file():
    with open("tests/transcoder.lic", "r") as f:
        content = f.read()
        content = content.replace("-----BEGIN TRANSCODER LICENSE-----", "").replace("-----END TRANSCODER LICENSE-----", "")
        base64_content = base64.b64decode(content)
        return json.loads(base64_content)

def create_session():
    with get_httpx_client() as client:
        # Read the actual license file
        license_data = read_license_file()
        
        # Test session creation with the actual license
        response = client.post(f"{BASE_URL}/create_session", json=license_data)
        print(response)
        assert response.status_code == 200
        data = response.json()
        assert "token" in data
        
        # Verify the token
        token = data["token"]
        decoded = verify_token(token)
        assert decoded is not None
        assert "session_id" in decoded
        assert "exp" in decoded
        
        return token

def test_session_timeout():
    with get_httpx_client() as client:
        # Create a session
        token = create_session()
        decoded = verify_token(token)
        assert decoded is not None
        session_id = decoded["session_id"]
        
        # Wait for more than the timeout period (1 minute)
        time.sleep(65)  # Wait for 65 seconds to ensure timeout
        
        # Try to send heartbeat - should fail with 404
        response = client.post(f"{BASE_URL}/heartbeat/{session_id}")
        assert response.status_code == 404
        assert response.json()["detail"] == "Session not found"
        
        # Try to create a new session with the same license - should succeed
        # because the old session was invalidated
        new_token = create_session()
        assert new_token != token  # Should be a different token 
import base64
import json
import pytest
import httpx
import logging
import jwt
from cryptography.hazmat.primitives import serialization

# Configure logging
logging.basicConfig(level=logging.INFO)

BASE_URL_SESSION = "https://127.0.0.1:8000"

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
        response = client.post(f"{BASE_URL_SESSION}/create_session", json=license_data)
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

def test_receive_heartbeat():
    with get_httpx_client() as client:
        # First create a session
        token = create_session()
        print("session created", token)
        
        # Extract session_id from token
        decoded = verify_token(token)
        assert decoded is not None
        session_id = decoded["session_id"]
        
        # Test heartbeat
        response = client.post(f"{BASE_URL_SESSION}/heartbeat/{session_id}")
        assert response.status_code == 200
        data = response.json()
        assert data["detail"] == "Heartbeat received"
        assert "token" in data
        
        # Verify the new token
        new_token = data["token"]
        new_decoded = verify_token(new_token)
        assert new_decoded is not None
        assert new_decoded["session_id"] == session_id

def test_heartbeat_invalid_session():
    with get_httpx_client() as client:
        response = client.post(f"{BASE_URL_SESSION}/heartbeat/invalid_session_id")
        assert response.status_code == 404
        assert response.json()["detail"] == "Session not found"


# Transcoder License Server

A FastAPI-based license server for managing transcoder sessions and licenses. The server handles license validation, session management, and provides heartbeat functionality to maintain active sessions.

## Features

- License validation and decoding
- Session management with concurrent session limits
- JWT-based session authentication
- Automatic session invalidation
- Heartbeat mechanism to maintain active sessions

## Prerequisites

- Python 3.11+
- SQLite3
- Ed25519 key pair for JWT signing/verification

## Installation

1. Clone the repository:
```bash
git clone <repository-url>
cd transcoder-license-server
```

2. Create and activate a virtual environment:
```bash
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
```

3. Install dependencies:
```bash
pip install -r requirements.txt
```

4. Generate Ed25519 key pair (if not already present):
```bash
# Generate private key
openssl genpkey -algorithm Ed25519 -out certificates/license_ed25519_private_key.der

# Generate public key from private key
openssl pkey -in certificates/license_ed25519_private_key.der -pubout -out certificates/license_ed25519_public_key.der
```

## Configuration

The server uses the following configuration:

- Database: SQLite3 (`sessions.db`)
- JWT Algorithm: EdDSA (Ed25519)
- Session expiration: 1 minute
- Heartbeat interval: 10 seconds
- Session invalidation: Sessions without heartbeat for 1 minute are invalidated

## HTTPS Configuration

To run the server with HTTPS, you'll need SSL certificates. You can either use self-signed certificates for development or proper SSL certificates for production.

### Generating Self-Signed Certificates (Development)

```bash
# Generate private key and certificate
openssl req -x509 -newkey rsa:4096 -keyout certificates/server.key -out certificates/server.crt -days 365 -nodes -subj "/CN=localhost"
```

### Running with HTTPS

1. Using uvicorn directly:
```bash
uvicorn app.session_app:session_app --reload --port 8000 --ssl-keyfile certificates/server.key --ssl-certfile certificates/server.crt
```

2. Using Docker:
```bash
docker run -p 8000:8000 \
  -v [your_dir]/server.key:/license_server/certificates/server.key \
  -v [your_dir]/certificates/server.crt:/license_server/certificates/server.crt \
  -v [your_dir]/certificates/license_ed25519_private_key.der:/license_server/certificates/license_ed25519_private_key.der \
  -v [your_dir]/certificates/license_ed25519_public_key.der:/license_server/certificates/license_ed25519_public_key.der \
  -v [your_dir]/certificates/license_encryption_key.bin:/license_server/certificates/license_encryption_key.bin \
  -v [your_dir]//sessions.db:/license_server/sessions.db \
  license-server:latest
```
- license_ed25519_private_key.der - private key, the same as used for transcoding appliance 
- license_ed25519_public_key.der - public key, the same as used for transcoding appliance 
- license_encryption_key.bin -- encryption key, the same as used for transcoding appliance 
- server.key / server.crt -- SSL certificates

In case volumes are not provided default (development) certifcates will be used. 


Note: When using self-signed certificates, clients will need to either:
- Add the certificate to their trusted certificates
- Disable certificate verification (not recommended for production)
- Use the `--insecure` flag with curl: `curl -k https://localhost:8000/...`

## Running the Server

Start the server with:
```bash
uvicorn app.session_app:session_app --reload --port 8000
```

The server will be available at `http://127.0.0.1:8000`

## Docker Usage

The server can be run using Docker. The Dockerfile is configured to use Python 3.11 slim as the base image and includes all necessary dependencies.

### Building the Docker Image

```bash
docker build -t license-server:latest .
```

### Running the Container

The container can be run in two ways:

1. Using embedded certificates (default):
```bash
docker run -p 8000:8000 license-server:latest
```

2. Using local certificates via volume mounts (recommended for development):
```bash
docker run -p 8000:8000 \
  -v $(pwd)/certificates/license_ed25519_private_key.der:/license_server/certificates/license_ed25519_private_key.der \
  -v $(pwd)/certificates/license_ed25519_public_key.der:/license_server/certificates/license_ed25519_public_key.der \
  -v $(pwd)/certificates/license_encryption_key.bin:/license_server/certificates/license_encryption_key.bin \
  -v $(pwd)/sessions.db:/license_server/sessions.db \
  license-server:latest
```

Using volume mounts allows you to:
- Update certificates without rebuilding the container
- Persist the sessions database between container restarts
- Use different certificates for different environments

## API Endpoints

### Create Session
```http
POST /create_session
Content-Type: application/json

{
    "data": {
        "id": "license-id",
        "name": "license-name",
        "ttl": 315532800000,
        "valid_since": 1735689600000
    },
    "meta": {
        "created_at": 1738704428399
    }
}
```

Response:
```json
{
    "token": "jwt-token"
}
```

### Heartbeat
```http
POST /heartbeat/{session_id}
```

Response:
```json
{
    "token": "new-jwt-token",
    "detail": "Heartbeat received"
}
```

## Testing

Run the tests with:
```bash
python -m pytest tests/ -v
```

Manual communication with the server using cURL:
```bash
curl -v localhost:8000/create_session -H "Content-Type: application/json" -d "$(tail -n +2 tests/transcoder.lic | head -n -1 | base64 -d)"

curl -v localhost:8000/heartbeat/8131b176-0b0c-4d4f-aece-a33179310975 -X POST
```

## License File Format

The test license file should be in the following format:
```
-----BEGIN TRANSCODER LICENSE-----
<base64-encoded-json>
-----END TRANSCODER LICENSE-----
```

The base64-encoded and encrypted JSON should contain:
```json
{
    "data": {
        "id": "license-id",
        "name": "license-name",
        "ttl": 315532800000,
        "valid_since": 1735689600000
    },
    "meta": {
        "created_at": 1738704428399
    }
}
```

## Error Handling

The server returns appropriate HTTP status codes:
- 200: Success
- 400: Bad Request (invalid license, maximum sessions reached)
- 404: Not Found (invalid session ID)

## Security

- JWT tokens are signed with Ed25519
- Sessions are automatically invalidated after 1 minute of inactivity
- License validation includes expiration and validity period checks
- Concurrent session limits are enforced per license 
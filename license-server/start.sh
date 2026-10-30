#!/bin/bash

# Check if both server.key and server.crt exist
if [[ -f "/license_server/certificates/server.key" && -f "/license_server/certificates/server.crt" ]]; then
    echo "Starting with HTTPS..."
    exec uvicorn app.session_app:session_app --host 0.0.0.0 --port 8000 \
        --ssl-keyfile /license_server/certificates/server.key \
        --ssl-certfile /license_server/certificates/server.crt
else
    echo "Starting without HTTPS..."
    exec uvicorn app.session_app:session_app --host 0.0.0.0 --port 8000
fi
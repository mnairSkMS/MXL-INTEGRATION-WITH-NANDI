from pathlib import Path
from fastapi import FastAPI, HTTPException, Depends
from sqlalchemy.orm import Session
from app.models.license import License, LicenseData, LicenseMeta
from app.utils.license_decoder import decode
from app.models.session import TranscoderSession
from .database import SessionLocal, init_db
from datetime import datetime, timedelta
import jwt
import uuid
import asyncio
from cryptography.hazmat.primitives import serialization


session_app = FastAPI()

# Load RSA keys
with open(Path(".") / "certificates" / "license_ed25519_private_key.der", "rb") as f:
    key_data = f.read()
    PRIVATE_KEY = serialization.load_der_private_key(key_data, password=None)

ALGORITHM = "EdDSA"

def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

@session_app.post("/create_session")
def create_session(license_file: dict, db: Session = Depends(get_db)):
    try:
        # Decode license file
        license_content = decode(license_file)
        print(license_content)
        # Convert to pydantic model
        license_model = License(data=LicenseData(**license_content['data']), 
                              meta=LicenseMeta(**license_content['meta']))
        print(license_model)
        if not license_model.is_valid():
            raise HTTPException(status_code=400, detail="License is expired or not yet valid")
        
        active_sessions_count = db.query(TranscoderSession).filter(
            TranscoderSession.license_id == license_model.data.id,
        ).count()
        if active_sessions_count >= license_model.data.max_sessions:
            raise HTTPException(status_code=400, detail="Maximum concurrent sessions reached")

        session_id = str(uuid.uuid4())
        new_session = TranscoderSession(license_id=license_model.data.id, session_id=session_id)
        db.add(new_session)
        db.commit()
    except Exception as e:
        print(e)
        raise HTTPException(status_code=400, detail=f"Invalid license file: {str(e)}")

    token_data = {"session_id": session_id, "exp": datetime.utcnow() + timedelta(minutes=1)}
    token = jwt.encode(token_data, PRIVATE_KEY, algorithm=ALGORITHM)
    return {"token": token}

@session_app.post("/heartbeat/{session_id}")
def receive_heartbeat(session_id: str, db: Session = Depends(get_db)):
    db_session = db.query(TranscoderSession).filter(TranscoderSession.session_id == session_id).first()
    if db_session is None:
        raise HTTPException(status_code=404, detail="Session not found")

    db_session.last_heartbeat = datetime.utcnow()
    db.commit()

    # Generate new token with updated expiration
    token_data = {"session_id": session_id, "exp": datetime.utcnow() + timedelta(minutes=1)}
    token = jwt.encode(token_data, PRIVATE_KEY, algorithm=ALGORITHM)
    return {"token": token, "detail": "Heartbeat received"}

# Function to invalidate sessions
def invalidate_sessions():
    db = SessionLocal()
    try:
        expiration_time = datetime.utcnow() - timedelta(minutes=1)
        
        inactive_sessions = db.query(TranscoderSession).filter(TranscoderSession.last_heartbeat < expiration_time).all()

        for session in inactive_sessions:
            db.delete(session)
        db.commit()
    finally:
        db.close()

# Background task to run session invalidation periodically
@session_app.on_event("startup")
async def startup_event():
    # Initialize database
    init_db()
    # Start session invalidation loop
    asyncio.create_task(run_invalidation_loop())

async def run_invalidation_loop():
    while True:
        invalidate_sessions()
        await asyncio.sleep(10)  # Use asyncio.sleep for non-blocking sleep 
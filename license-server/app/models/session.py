from sqlalchemy import Column, Integer, String, DateTime
from app.database import Base
from datetime import datetime

class TranscoderSession(Base):
    __tablename__ = 'transcoder_sessions'

    id = Column(Integer, primary_key=True, index=True)
    license_id = Column(String, nullable=False)
    session_id = Column(String, unique=True, index=True, nullable=False)
    created_at = Column(DateTime, default=datetime.utcnow)
    last_heartbeat = Column(DateTime, default=datetime.utcnow)


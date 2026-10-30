from pydantic import BaseModel, Field
from typing import Optional
from datetime import datetime

class LicenseData(BaseModel):
    """License data model."""
    id: str = Field(..., description="Unique identifier for the license")
    name: str = Field(..., description="Name of the license")
    ttl: int = Field(..., description="Time to live in milliseconds")
    valid_since: int = Field(..., description="Timestamp when the license becomes valid")
    max_sessions: int = Field(default=1, ge=1, description="Number of parallel sessions allowed")

class LicenseMeta(BaseModel):
    """License metadata model."""
    created_at: int = Field(..., description="Timestamp when the license was created")

class License(BaseModel):
    """Complete license model."""
    data: LicenseData
    meta: LicenseMeta

    def is_valid(self) -> bool:
        """Check if the license is currently valid."""
        now = int(datetime.now().timestamp() * 1000)  # Current time in milliseconds
        return (
            now >= self.data.valid_since and
            now <= self.data.valid_since + self.data.ttl
        )

    def time_remaining(self) -> Optional[int]:
        """Get remaining time in milliseconds, or None if expired."""
        if not self.is_valid():
            return None
        now = int(datetime.now().timestamp() * 1000)
        return (self.data.valid_since + self.data.ttl) - now 
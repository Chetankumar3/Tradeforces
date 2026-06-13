"""Routes for main service."""

from typing import Optional
from fastapi import APIRouter, HTTPException, status, Depends
from pydantic import BaseModel
import google.auth
from google.cloud import storage
from google.cloud import pubsub_v1
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select
import sys
import os

# Add shared_core to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from shared_core.DB_models import User, Submission, Base
from shared_core.auth import create_jwt_token, get_current_user, hash_password, verify_password_hash
from shared_core.schema_mapper import SchemaMapper
from shared_core.core import create_async_db_engine, async_sessionmaker
from .config import settings

router = APIRouter()
schema_mapper = SchemaMapper()


# Pydantic models
class LoginRequest(BaseModel):
    """Login request model."""
    username: str
    password: str


class LoginResponse(BaseModel):
    """Login response model."""
    access_token: str
    token_type: str = "bearer"


class SubmitResponse(BaseModel):
    """Presigned URL response."""
    presigned_url: str
    submission_id: int


class UploadCompleteRequest(BaseModel):
    """Upload completion request."""
    submission_id: int

class RegisterRequest(BaseModel):
    """Registration request model."""
    username: str
    name: str
    email: str
    password: str


class RegisterResponse(BaseModel):
    """Registration response model."""
    access_token: str
    token_type: str = "bearer"
    user_id: int


# Helper: Get async session
async def get_db_session() -> AsyncSession:
    """Get database session."""
    engine = create_async_db_engine()
    SessionLocal = async_sessionmaker(engine, class_=AsyncSession, expire_on_commit=False)
    async with SessionLocal() as session:
        yield session


def get_gcs_client():
    """Get authenticated GCS client using ADC."""
    credentials, project_id = google.auth.default(
        scopes=["https://www.googleapis.com/auth/cloud-platform"]
    )
    return storage.Client(project=project_id, credentials=credentials)


def get_pubsub_publisher():
    """Get Pub/Sub publisher client."""
    credentials, project_id = google.auth.default(
        scopes=["https://www.googleapis.com/auth/cloud-platform"]
    )
    return pubsub_v1.PublisherClient(credentials=credentials)


def verify_password(stored_hash: str, password: str) -> bool:
    """Verify password against bcrypt hash."""
    return verify_password_hash(password, stored_hash)

@router.post("/register", response_model=RegisterResponse, status_code=201)
async def register(request: RegisterRequest, session: AsyncSession = Depends(get_db_session)):
    """Register a new user and return a JWT token."""

    # Check username uniqueness
    result = await session.execute(
        select(User).where(User.username == request.username)
    )
    if result.scalars().first():
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="Username already taken"
        )

    # Check email uniqueness
    result = await session.execute(
        select(User).where(User.email == request.email)
    )
    if result.scalars().first():
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="Email already registered"
        )

    # Create user with hashed password
    user = User(
        username=request.username,
        name=request.name,
        email=request.email,
        password_hash=hash_password(request.password)
    )
    session.add(user)
    await session.commit()
    await session.refresh(user)

    token = create_jwt_token(user.id)
    return RegisterResponse(access_token=token, user_id=user.id)

    
@router.post("/login/credentials", response_model=LoginResponse)
async def login(request: LoginRequest, session: AsyncSession = Depends(get_db_session)):
    """Authenticate user and return JWT token."""
    result = await session.execute(
        select(User).where(User.username == request.username)
    )
    user = result.scalars().first()
    
    if not user or not verify_password(user.password_hash, request.password):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid username or password"
        )
    
    token = create_jwt_token(user.id)
    return LoginResponse(access_token=token)


@router.get("/submit/{submission_id}", response_model=SubmitResponse)
async def submit(
    submission_id: int,
    user_id: int = Depends(get_current_user),
    session: AsyncSession = Depends(get_db_session)
):
    """Create submission record and return presigned GCS upload URL."""
    
    # Create submission record with status 'uploading'
    submission = Submission(
        user_id=user_id,
        gcs_url=f"gs://{settings.gcs_bucket_name}/submissions/{user_id}/{submission_id}.zip",
        status="uploading"
    )
    session.add(submission)
    await session.commit()
    await session.refresh(submission)
    
    # Generate presigned URL
    client = get_gcs_client()
    bucket = client.bucket(settings.gcs_bucket_name)
    blob = bucket.blob(f"submissions/{user_id}/{submission_id}.zip")
    
    presigned_url = blob.generate_signed_url(
        version="v4",
        expiration=settings.presigned_url_expiry_minutes * 60,
        method="PUT"
    )
    
    return SubmitResponse(
        presigned_url=presigned_url,
        submission_id=submission.id
    )


@router.post("/upload_complete/{submission_id}")
async def upload_complete(
    submission_id: int,
    user_id: int = Depends(get_current_user),
    session: AsyncSession = Depends(get_db_session)
):
    """Mark upload complete and publish to Queue 1."""
    
    # Get submission
    result = await session.execute(
        select(Submission).where(
            (Submission.id == submission_id) & (Submission.user_id == user_id)
        )
    )
    submission = result.scalars().first()
    
    if not submission:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Submission not found"
        )
    
    # Update status
    submission.status = "queued_for_microVM_creation"
    await session.commit()
    
    # Publish to Queue 1
    publisher = get_pubsub_publisher()
    topic_path = publisher.topic_path(settings.project_id, settings.queue1_name)
    
    message_data = {
        "gcs_url": submission.gcs_url,
        "submission_id": submission_id,
        "user_id": user_id
    }
    
    # Map using schema
    queue1_message = schema_mapper.to_queue1(message_data)
    
    # Publish
    import json
    publisher.publish(
        topic_path,
        json.dumps(queue1_message).encode("utf-8")
    )
    
    return {"status": "success", "message": "Submission queued for microVM creation"}

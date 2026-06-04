"""Main FastAPI application."""

from fastapi import FastAPI
from fastapi.responses import JSONResponse
from contextlib import asynccontextmanager

from .routes import router


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan."""
    print("Main service starting...")
    yield
    print("Main service shutting down...")


app = FastAPI(
    title="Tradeforces Main API",
    description="Main API Gateway for submission upload and microVM creation",
    version="1.0.0",
    lifespan=lifespan
)

app.include_router(router)


@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return JSONResponse({"status": "healthy"})


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)

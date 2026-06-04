"""FastAPI application for VM Creator service."""

import asyncio
import logging
from contextlib import asynccontextmanager
from fastapi import FastAPI
from fastapi.responses import JSONResponse

from .worker import run_worker

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

worker_task: asyncio.Task = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan - start and stop worker."""
    global worker_task
    
    logger.info("VM Creator service starting...")
    
    # Start worker in background
    worker_task = asyncio.create_task(run_worker())
    
    yield
    
    logger.info("VM Creator service shutting down...")
    if worker_task:
        worker_task.cancel()
        try:
            await worker_task
        except asyncio.CancelledError:
            pass


app = FastAPI(
    title="Tradeforces VM Creator",
    description="Async worker service for microVM creation and deployment",
    version="1.0.0",
    lifespan=lifespan
)


@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return JSONResponse({"status": "healthy"})


@app.get("/metrics")
async def metrics():
    """Metrics endpoint."""
    # Could return Prometheus metrics here
    return JSONResponse({
        "service": "vm_creator",
        "status": "running"
    })


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8001)

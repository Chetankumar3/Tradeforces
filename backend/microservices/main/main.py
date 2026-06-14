from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from contextlib import asynccontextmanager

from shared_core.core import init_db

from .routes import router


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan."""
    print("Main service starting...")
    try:
        db_ready = await init_db()
        if db_ready:
            print("Database schema initialized.")
        else:
            print("Database unavailable at startup; continuing without DB initialization.")
    except Exception as exc:
        print(f"Database initialization unavailable, continuing without DB: {exc}")
    yield
    print("Main service shutting down...")


app = FastAPI(
    title="Tradeforces Main API",
    description="Main API Gateway for submission upload and microVM creation",
    version="1.0.0",
    lifespan=lifespan,
    root_path="/main"
)

origins = [
    "*"
]

app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(router)


@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return JSONResponse({"status": "healthy"})


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)

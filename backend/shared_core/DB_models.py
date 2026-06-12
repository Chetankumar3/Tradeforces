from sqlalchemy import String, Numeric, ForeignKey, create_engine
from sqlalchemy.orm import Mapped, mapped_column, declarative_base
from decimal import Decimal
from typing import Optional

Base = declarative_base()

class User(Base):
    __tablename__ = "user"

    id: Mapped[int] = mapped_column(primary_key=True)
    username: Mapped[str] = mapped_column(String(50), unique=True, index=True)
    name: Mapped[str] = mapped_column(String(100))
    email: Mapped[str] = mapped_column(String(100), unique=True, index=True)
    password_hash: Mapped[str] = mapped_column(String(200))


class Submission(Base):
    __tablename__ = "submission"

    id: Mapped[int] = mapped_column(primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("user.id"), index=True)
    gcs_url: Mapped[str] = mapped_column(String(200))
    status: Mapped[str] = mapped_column(String(20))
    # "uploading", "queued_for_microVM_creation", "queued_for_loadgen_setup", "test_running", "test_completed", "error"
    ack_latency_p50: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    ack_latency_p90: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    ack_latency_p99: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    exec_latency_p50: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    exec_latency_p90: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    exec_latency_p99: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    correctness_score: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    max_throughput: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    composite_score: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True) 
    error_message: Mapped[Optional[str]] = mapped_column(String(500), nullable=True)

class IntermediateScores(Base):
    __tablename__ = "intermediate_scores"

    id: Mapped[int] = mapped_column(primary_key=True)
    submission_id: Mapped[int] = mapped_column(ForeignKey("submission.id"), index=True)
    time_lapse: Mapped[int] = mapped_column() # in seconds
    ack_latency_p50: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    ack_latency_p90: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    ack_latency_p99: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    exec_latency_p50: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    exec_latency_p90: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    exec_latency_p99: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    correctness_score: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    max_throughput: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True)
    composite_score: Mapped[Optional[Decimal]] = mapped_column(Numeric(8, 6), nullable=True) 
    error_message: Mapped[Optional[str]] = mapped_column(String(500), nullable=True)
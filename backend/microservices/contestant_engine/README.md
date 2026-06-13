# FIX Protocol Trading Gateway

A high-performance FIX (Financial Information eXchange) Protocol 4.4 trading gateway implemented in modern C++20. This system provides a complete order management and execution infrastructure with sub-millisecond latency, suitable for institutional-grade trading applications.

## Overview
This trading gateway implements the FIX 4.4 protocol specification and provides a comprehensive solution for electronic trading. It includes session management, order lifecycle handling, market data distribution, risk management, and a simulated matching engine. The system is designed for low-latency operation and high throughput, capable of processing millions of orders per second in pure computational throughput.
## Performance Benchmarks

Tested on MacBook Air M4 (10-core CPU, 16GB RAM).

### Latency Performance

| Metric | Value | Industry Target |
|--------|-------|-----------------|
| P50 (Median) | 350 μs | < 500 μs |
| P95 | 610 μs | < 2 ms |
| P99 | 633 μs | < 5 ms |

All latency percentiles are sub-millisecond, which puts this gateway in the same league as professional low-latency trading systems.

### Throughput Performance

| Test Scenario | Orders/Second |
|--------------|---------------|
| Pure CPU Processing | 24-40M |
| Multi-Threaded (8 cores) | 250K |
| Risk Checks | 14M |

The pure CPU numbers (24-40M orders/sec) represent theoretical computational capacity in ideal conditions. In real production with network I/O, disk persistence, and market data distribution, expect throughput around 20K-100K orders/sec - still plenty fast for most trading strategies.

---

## Core Features

### FIX Protocol Engine
- Full FIX 4.4 protocol implementation
- Session management (Initiator and Acceptor modes)
- Logon/Logout handling with sequence number management
- Heartbeat mechanism with configurable intervals
- Message parsing, validation, and serialization
- Multi-session support with independent lifecycle management
- Automatic reconnection with configurable intervals

### Order Management System
- Complete order lifecycle management
- Supported order types: Market, Limit, Stop
- Order operations: New Order Single, Cancel, Cancel/Replace
- Execution report generation with detailed fill information
- Position tracking with real-time P&L calculation
- Multi-account support with isolated positions
- Order book management with price-time priority

### Market Data Distribution
- Market Data Request/Snapshot/Incremental Refresh (FIX messages V, W, X)
- Multi-level order book (depth of market)
- Real-time quote updates and trade publication
- Symbol subscription management
- Configurable market data throttling

### Simulated Exchange
- Price-time priority matching engine
- Realistic price generation with random walk model
- Bid-ask spread simulation
- Stop order triggering
- Support for multiple trading instruments
- Configurable volatility and tick sizes

### Risk Management
- Pre-trade risk checks with configurable limits
- Order size and position limit enforcement
- Daily loss limit monitoring
- Credit limit validation
- Fat finger detection (price deviation checks)
- Concentration risk monitoring
- Account-level and symbol-level controls

### Performance & Monitoring
- Comprehensive metrics collection (latency percentiles, throughput, fill rates)
- Prometheus-compatible metrics export
- Administrative REST API for system management
- Real-time health monitoring
- Latency tracking (P50, P95, P99)

### Persistence & Recovery
- FIX message storage for audit trails
- Sequence number persistence and recovery
- Order book snapshots
- Graceful shutdown with state preservation
- Message replay capability

## Technology Stack

- **Language**: C++20 with modern features
- **Networking**: Boost.Asio 1.89.0 for asynchronous I/O
- **Configuration**: nlohmann/json 3.12.0 for JSON parsing
- **Logging**: Custom SimpleLogger with file and console output
- **Testing**: Google Test framework with 144 unit tests
- **Build System**: CMake 3.20+ for cross-platform builds
- **Compiler**: AppleClang 17.0 (Clang/LLVM compatible)
- **Performance**: Object pools, lock-free queues, memory-mapped files

## Prerequisites

### macOS
```bash
brew install cmake boost nlohmann-json googletest
```

**Linux**:
```bash
sudo apt install -y cmake build-essential libboost-all-dev \
    nlohmann-json3-dev libgtest-dev
```

### Build and Run

```bash
# Build
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run tests
./fix_engine_tests

# Run benchmarks
./performance_benchmark

# Start gateway
./fix_gateway
```

## Configuration

Configuration files are located in `config/` directory.

### FIX Session (`config/fix_config.json`)

```json
{
  "sessions": [{
    "sessionId": "CLIENT_SESSION_1",
    "senderCompId": "CLIENT1",
    "targetCompId": "GATEWAY",
    "heartbeatInterval": 30,
    "isInitiator": false,
    "socketAcceptPort": 9876
  }]
}
```

### Trading Rules (`config/trading_rules.json`)

```json
{
  "maxOrderSize": 1000000.0,
  "minOrderSize": 1.0,
  "maxPositionSize": 10000000.0,
  "tradableSymbols": ["AAPL", "GOOGL", "MSFT"]
}
```

## Architecture

### System Design

```
Clients (FIX 4.4)
    ↓
FIX Engine (Parser, Validator, Session Manager)
    ↓
Order Manager (Validation, Order Book, Positions)
    ↓
Risk Manager ←→ Matching Engine
    ↓
Market Data Manager
```

### Data Flow

**Order Processing**:
```
FIX Message → Parse → Validate → Risk Check → Order Book → 
Matching Engine → Execution Report
```

**Market Data**:
```
Price Update → Matching Engine → Market Data Manager → 
Subscribers → FIX Message
```

### Threading Model

- **Main Thread**: Application lifecycle
- **I/O Thread Pool**: Boost.Asio workers (network operations)
- **Lock-Free Structures**: High-frequency message passing
- **Thread-Safe Components**: Synchronized order books and positions

## Testing

144 unit tests across 17 test suites covering:
- FIX engine (parsing, sessions, validation)
- Order management (lifecycle, book, positions)
- Market data (subscriptions, snapshots)
- Risk management (limits, checks)
- Performance (latency, concurrency)

```bash
# Run all tests
./build/fix_engine_tests

# Run specific suite
./build/fix_engine_tests --gtest_filter="RiskManager*"
```


## Troubleshooting

**Port in use**:
```bash
lsof -i :9876  # Check port
# Or change port in config/fix_config.json
```

**Boost not found**:
```bash
export BOOST_ROOT=/path/to/boost
```

## Project Highlights

- **Sub-millisecond latency** (P50: 350 μs, P99: 633 μs)
- **High throughput** (250K orders/sec multi-threaded, 40M/sec pure CPU)

---

**Use Cases**: Market making, statistical arbitrage, institutional trading.
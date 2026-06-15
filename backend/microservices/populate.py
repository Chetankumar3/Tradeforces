import json
from confluent_kafka import Producer

# ==========================================
# CONFIGURATION VARIABLES
# ==========================================
JSON_FILE_PATH = "messages.json"
TOPIC_NAME     = "20"
BROKERS        = "d8ldqou8rvp9ucmhj1tg.any.ap-south-1.mpx.prd.cloud.redpanda.com:9092"

USE_AUTH = True
RP_USER  = "rp_user"
RP_PASS  = "rp_123"

# ==========================================
# INITIALIZE PRODUCER
# ==========================================
conf = {
    'bootstrap.servers': BROKERS,
    'client.id': 'python-json-publisher'
}

if USE_AUTH:
    conf.update({
        'security.protocol': 'SASL_SSL',
        'sasl.mechanisms': 'SCRAM-SHA-256',
        'sasl.username': RP_USER,
        'sasl.password': RP_PASS
    })

producer = Producer(conf)

def delivery_report(err, msg):
    if err is not None:
        print(f"[ERROR] Message delivery failed: {err}")
    else:
        print(f"[SUCCESS] Message delivered to {msg.topic()} [{msg.partition()}] at offset {msg.offset()}")

# ==========================================
# FIX CHECKSUM CALCULATOR
# ==========================================
def recalculate_fix_checksum(payload: str) -> str:
    """
    Finds the '10=' tag, calculates the sum of all ASCII characters 
    preceding it (including the SOH delimiter), and returns the updated payload.
    """
    checksum_marker = '\x0110='
    idx = payload.rfind(checksum_marker)
    
    if idx == -1:
        return payload

    # Extract everything up to and including the SOH before '10='
    prefix = payload[:idx + 1]
    
    # Sum ASCII values
    total_sum = sum(ord(c) for c in prefix)
    calculated_checksum = total_sum % 256
    
    # Return reassembled payload with 3-digit zero-padded checksum
    return f"{prefix}10={calculated_checksum:03d}\x01"

# ==========================================
# READ JSON AND PUBLISH
# ==========================================
def main():
    try:
        with open(JSON_FILE_PATH, 'r') as file:
            messages = json.load(file)
            
        print(f"Loaded {len(messages)} messages from {JSON_FILE_PATH}. Starting publish...")

        for idx, item in enumerate(messages[:20]):
            val_payload = item.get("value", {}).get("payload")
            key_payload = str(item.get("key", {}).get("payload", ""))

            if not val_payload:
                print(f"Skipping index {idx}: No value payload found.")
                continue

            # Update the checksum in-memory
            val_payload = recalculate_fix_checksum(val_payload)

            producer.produce(
                topic=TOPIC_NAME,
                key=key_payload.encode('utf-8') if key_payload else None,
                value=val_payload.encode('utf-8'),
                callback=delivery_report
            )

            producer.poll(0)

        print("Flushing messages to broker...")
        producer.flush()
        print("All messages successfully published!")

    except FileNotFoundError:
        print(f"[ERROR] Could not find file {JSON_FILE_PATH}")
    except json.JSONDecodeError as e:
        print(f"[ERROR] Invalid JSON in {JSON_FILE_PATH}: {e}")
    except Exception as e:
        print(f"[ERROR] An unexpected error occurred: {e}")

if __name__ == '__main__':
    main()
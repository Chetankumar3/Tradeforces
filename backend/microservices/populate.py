import json
from confluent_kafka import Producer

# ==========================================
# CONFIGURATION VARIABLES
# ==========================================
JSON_FILE_PATH = "messages.json"
TOPIC_NAME     = "20"  # Assuming '20' is your topic based on your previous logs
BROKERS        = "d8ldqou8rvp9ucmhj1tg.any.ap-south-1.mpx.prd.cloud.redpanda.com:9092" # Replace with your Redpanda broker IP(s)

# If you are using SASL/SCRAM authentication (uncomment and fill these in if needed)
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
        'security.protocol': 'SASL_SSL', # Or SASL_PLAINTEXT depending on your setup
        'sasl.mechanisms': 'SCRAM-SHA-256',
        'sasl.username': RP_USER,
        'sasl.password': RP_PASS
    })

producer = Producer(conf)

# Optional delivery callback to confirm successful publishing
def delivery_report(err, msg):
    if err is not None:
        print(f"[ERROR] Message delivery failed: {err}")
    else:
        print(f"[SUCCESS] Message delivered to {msg.topic()} [{msg.partition()}] at offset {msg.offset()}")

# ==========================================
# READ JSON AND PUBLISH
# ==========================================
def main():
    try:
        with open(JSON_FILE_PATH, 'r') as file:
            messages = json.load(file)
            
        print(f"Loaded {len(messages)} messages from {JSON_FILE_PATH}. Starting publish...")

        for idx, item in enumerate(messages[:20]):  # Limit to first 20 messages
            # Extract the raw FIX payload
            val_payload = item.get("value", {}).get("payload")
            
            # Extract the key if it exists (converting to string as Kafka expects bytes/strings)
            key_payload = str(item.get("key", {}).get("payload", ""))

            if not val_payload:
                print(f"Skipping index {idx}: No value payload found.")
                continue

            # Publish to Redpanda
            producer.produce(
                topic=TOPIC_NAME,
                key=key_payload.encode('utf-8') if key_payload else None,
                value=val_payload.encode('utf-8'),
                callback=delivery_report
            )

            # Periodically poll to handle delivery callbacks and avoid queue overflow
            producer.poll(0)

        # Wait for all asynchronous messages to be delivered
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
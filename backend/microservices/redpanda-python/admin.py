from confluent_kafka.admin import AdminClient, NewTopic
from confluent_kafka import KafkaException


conf = {
    'bootstrap.servers': 'd8ldqou8rvp9ucmhj1tg.any.ap-south-1.mpx.prd.cloud.redpanda.com:9092',
    'security.protocol': 'SASL_SSL',
    'sasl.mechanisms': 'SCRAM-SHA-256',
    'sasl.username': 'rp_user',
    'sasl.password': 'rp_123'
}

admin = AdminClient(conf)

# 2. Define the topic. 
new_topic = NewTopic("demo-topic", num_partitions=1, replication_factor=3)

# 3. Call create_topics. This returns a dict of {topic_name: Future}
futures = admin.create_topics([new_topic])

# 4. Wait for the Future to resolve to check for errors
for topic_name, future in futures.items():
    try:
        future.result()  # Blocks until the topic is created or fails
        print(f"Created topic: {topic_name}")
    except KafkaException as e:
        error_code = e.args[0].name()
        if error_code == "TOPIC_ALREADY_EXISTS":
            print(f"Topic '{topic_name}' already exists")
        else:
            print(f"Failed to create topic '{topic_name}': {e}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
"""Dynamic schema mapping for Pub/Sub message translation."""

import json
import os
from typing import Dict, Any

SCHEMA_FILE = os.path.join(os.path.dirname(__file__), "schema.json")


def load_schema() -> Dict[str, Dict[str, str]]:
    """Load schema mapping from schema.json."""
    with open(SCHEMA_FILE, "r") as f:
        return json.load(f)


class SchemaMapper:
    """Maps internal field names to/from Pub/Sub message keys."""
    
    def __init__(self):
        self.schema = load_schema()
    
    def to_queue1(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """Convert internal field names to Queue 1 message format."""
        mapping = self.schema.get("queue1_keys", {})
        result = {}
        
        for internal_key, queue_key in mapping.items():
            if internal_key in data:
                result[queue_key] = data[internal_key]
        
        return result
    
    def from_queue1(self, message: Dict[str, Any]) -> Dict[str, Any]:
        """Convert Queue 1 message format to internal field names."""
        mapping = self.schema.get("queue1_keys", {})
        reverse_mapping = {v: k for k, v in mapping.items()}
        result = {}
        
        for queue_key, internal_key in reverse_mapping.items():
            if queue_key in message:
                result[internal_key] = message[queue_key]
        
        return result
    
    def to_queue2(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """Convert internal field names to Queue 2 message format."""
        mapping = self.schema.get("queue2_keys", {})
        result = {}
        
        for internal_key, queue_key in mapping.items():
            if internal_key in data:
                result[queue_key] = data[internal_key]
        
        return result
    
    def from_queue2(self, message: Dict[str, Any]) -> Dict[str, Any]:
        """Convert Queue 2 message format to internal field names."""
        mapping = self.schema.get("queue2_keys", {})
        reverse_mapping = {v: k for k, v in mapping.items()}
        result = {}
        
        for queue_key, internal_key in reverse_mapping.items():
            if queue_key in message:
                result[internal_key] = message[queue_key]
        
        return result

#!/usr/bin/env python3
"""
Test script for cancel order validation using execution reports.
This script tests the end-to-end flow of cancel order confirmation.
"""

import zmq
import json
import time
import threading
import logging

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class CancelValidationTest:
    def __init__(self):
        self.context = zmq.Context()
        
        # Order sender socket (PUSH to trading engine)
        self.order_socket = self.context.socket(zmq.PUSH)
        self.order_socket.connect("tcp://127.0.0.1:5601")
        
        # Report listener socket (SUB from trading engine)
        self.report_socket = self.context.socket(zmq.SUB)
        self.report_socket.connect("tcp://127.0.0.1:5602")
        self.report_socket.setsockopt(zmq.SUBSCRIBE, b"exec.report")
        self.report_socket.setsockopt(zmq.SUBSCRIBE, b"exec.fill")
        
        # Set timeout for non-blocking operations
        self.report_socket.setsockopt(zmq.RCVTIMEO, 5000)  # 5 second timeout
        
        self.running = False
        self.received_reports = []
        
    def send_place_order(self, cl_id):
        """Send a place order to create an order that can be cancelled."""
        order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "place",
            "venue_type": "cex",
            "venue": "bybit",
            "product_type": "spot",
            "ts_ns": int(time.time() * 1_000_000_000),
            "symbol": "ETHUSDT",
            "side": "buy",
            "order_type": "limit",
            "time_in_force": "GTC",
            "price": 2000.0,
            "size": 0.01,
            "stop_price": 0.0,
            "reduce_only": False,
            "tags": {
                "test": "cancel_validation",
                "source": "python_test"
            }
        }
        
        message = json.dumps(order)
        self.order_socket.send_string(message)
        logger.info(f"Sent place order: {cl_id}")
        
    def send_cancel_order(self, cl_id, target_cl_id):
        """Send a cancel order for the specified target order."""
        cancel_order = {
            "version": 1,
            "cl_id": cl_id,
            "action": "cancel",
            "venue_type": "cex", 
            "venue": "bybit",
            "product_type": "spot",
            "ts_ns": int(time.time() * 1_000_000_000),
            "symbol": "ETHUSDT",
            "side": "buy",
            "order_type": "limit",
            "time_in_force": "GTC",
            "price": 0.0,
            "size": 0.0,
            "stop_price": 0.0,
            "reduce_only": False,
            "tags": {
                "cl_id_to_cancel": target_cl_id,
                "test": "cancel_validation",
                "source": "python_test"
            }
        }
        
        message = json.dumps(cancel_order)
        self.order_socket.send_string(message)
        logger.info(f"Sent cancel order: {cl_id} (cancelling {target_cl_id})")
        
    def listen_for_reports(self):
        """Listen for execution reports from the trading engine."""
        logger.info("Starting execution report listener...")
        self.running = True
        
        while self.running:
            try:
                # Receive multipart message (topic + payload)
                topic = self.report_socket.recv_string(zmq.NOBLOCK)
                payload = self.report_socket.recv_string(zmq.NOBLOCK)
                
                logger.info(f"Received report on topic '{topic}': {payload}")
                
                try:
                    report = json.loads(payload)
                    self.received_reports.append((topic, report))
                    
                    if topic == "exec.report":
                        status = report.get("status", "unknown")
                        cl_id = report.get("cl_id", "unknown") 
                        reason = report.get("reason_text", "")
                        logger.info(f"Execution Report - Order: {cl_id}, Status: {status}, Reason: {reason}")
                        
                except json.JSONDecodeError:
                    logger.error(f"Failed to parse JSON: {payload}")
                    
            except zmq.Again:
                # No message available, continue
                time.sleep(0.1)
            except Exception as e:
                logger.error(f"Error in report listener: {e}")
                break
                
        logger.info("Execution report listener stopped")
        
    def run_test(self):
        """Run the complete cancel validation test."""
        logger.info("Starting cancel validation test...")
        
        # Start report listener in background thread
        report_thread = threading.Thread(target=self.listen_for_reports)
        report_thread.daemon = True
        report_thread.start()
        
        time.sleep(1)  # Wait for listener to start
        
        # Test scenario: Place order then cancel it
        test_id = int(time.time())
        place_cl_id = f"test_place_{test_id}"
        cancel_cl_id = f"test_cancel_{test_id}"
        
        logger.info("=== Test Scenario: Place + Cancel ===")
        
        # Step 1: Send place order
        self.send_place_order(place_cl_id)
        time.sleep(2)  # Wait for place response
        
        # Step 2: Send cancel order
        self.send_cancel_order(cancel_cl_id, place_cl_id)
        time.sleep(3)  # Wait for cancel response
        
        # Stop listening
        self.running = False
        time.sleep(1)
        
        # Analyze results
        self.analyze_results(place_cl_id, cancel_cl_id)
        
    def analyze_results(self, place_cl_id, cancel_cl_id):
        """Analyze the received reports to validate cancel confirmation."""
        logger.info("=== Test Results Analysis ===")
        
        place_reports = [r for _, r in self.received_reports if r.get("cl_id") == place_cl_id]
        cancel_reports = [r for _, r in self.received_reports if r.get("cl_id") == cancel_cl_id]
        
        logger.info(f"Place order reports ({place_cl_id}): {len(place_reports)}")
        for report in place_reports:
            status = report.get("status", "unknown")
            reason = report.get("reason_text", "")
            logger.info(f"  - Status: {status}, Reason: {reason}")
            
        logger.info(f"Cancel order reports ({cancel_cl_id}): {len(cancel_reports)}")
        for report in cancel_reports:
            status = report.get("status", "unknown")  
            reason = report.get("reason_text", "")
            logger.info(f"  - Status: {status}, Reason: {reason}")
            
        # Validation checks
        if cancel_reports:
            cancel_status = cancel_reports[0].get("status", "unknown")
            if cancel_status in ["accepted", "rejected"]:
                logger.info("✅ SUCCESS: Cancel order received execution report")
                logger.info(f"✅ Cancel confirmation status: {cancel_status}")
                return True
            else:
                logger.error(f"❌ FAILURE: Unexpected cancel status: {cancel_status}")
        else:
            logger.error("❌ FAILURE: No cancel execution reports received")
            
        return False
        
    def cleanup(self):
        """Clean up ZMQ resources."""
        self.order_socket.close()
        self.report_socket.close()
        self.context.term()

if __name__ == "__main__":
    test = CancelValidationTest()
    
    try:
        success = test.run_test()
        if success:
            logger.info("🎉 Cancel validation test PASSED!")
        else:
            logger.error("💥 Cancel validation test FAILED!")
    except KeyboardInterrupt:
        logger.info("Test interrupted by user")
    except Exception as e:
        logger.error(f"Test failed with exception: {e}")
    finally:
        test.cleanup()
        logger.info("Test cleanup complete")

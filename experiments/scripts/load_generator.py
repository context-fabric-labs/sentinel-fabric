#!/usr/bin/env python3
"""
Load Generator for Control Plane Experiments

Generates synthetic load for benchmarking the control plane.
Supports multiple routing strategies and workload mixes.
"""

import argparse
import json
import random
import time
import requests
import statistics
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor
from collections import defaultdict

class LoadGenerator:
    def __init__(self, base_url, strategy='random'):
        self.base_url = base_url
        self.strategy = strategy
        self.sessions = {}
        self.results = defaultdict(list)
        
    def generate_session_id(self):
        return f"session-{random.randint(1000, 9999)}"
    
    def get_routing_headers(self, session_id=None):
        headers = {}
        if session_id and self.strategy == 'sticky':
            headers['X-Session-ID'] = session_id
        return headers
    
    def send_request(self, session_id=None, prompt_tokens=512, max_tokens=256):
        """Send a single request to the control plane"""
        url = f"{self.base_url}/v1/completions"
        
        payload = {
            "model": "llama-8b",
            "prompt": "x" * (prompt_tokens * 4),  # Approximate token length
            "max_tokens": max_tokens
        }
        
        headers = self.get_routing_headers(session_id)
        headers['Content-Type'] = 'application/json'
        
        start = time.time()
        try:
            response = requests.post(url, json=payload, headers=headers, timeout=30)
            latency_ms = (time.time() - start) * 1000
            
            result = {
                'status': response.status_code,
                'latency_ms': latency_ms,
                'session_id': session_id,
                'timestamp': datetime.now().isoformat()
            }
            
            if response.status_code == 200:
                data = response.json()
                result['tokens'] = data.get('usage', {}).get('total_tokens', 0)
            else:
                result['error'] = response.text
            
            return result
            
        except Exception as e:
            return {
                'status': 0,
                'latency_ms': (time.time() - start) * 1000,
                'error': str(e),
                'session_id': session_id,
                'timestamp': datetime.now().isoformat()
            }
    
    def run_workload(self, rps, duration, sessions, prompt_tokens, max_tokens):
        """Run workload with specified parameters"""
        print(f"Starting workload: {rps} RPS for {duration}s with {sessions} sessions")
        
        session_ids = [self.generate_session_id() for _ in range(sessions)]
        requests_per_session = (rps * duration) // sessions
        
        start_time = time.time()
        total_requests = 0
        successful_requests = 0
        
        with ThreadPoolExecutor(max_workers=100) as executor:
            futures = []
            
            for i in range(int(rps * duration)):
                session_id = random.choice(session_ids)
                future = executor.submit(
                    self.send_request,
                    session_id=session_id,
                    prompt_tokens=prompt_tokens,
                    max_tokens=max_tokens
                )
                futures.append(future)
                
                total_requests += 1
                
                # Rate limiting
                elapsed = time.time() - start_time
                expected = i / rps
                if elapsed < expected:
                    time.sleep(expected - elapsed)
            
            # Collect results
            for future in futures:
                result = future.result()
                self.results['latencies'].append(result['latency_ms'])
                self.results['statuses'].append(result['status'])
                
                if result['status'] == 200:
                    successful_requests += 1
        
        duration_actual = time.time() - start_time
        
        return {
            'total_requests': total_requests,
            'successful_requests': successful_requests,
            'duration_seconds': duration_actual,
            'actual_rps': total_requests / duration_actual,
            'success_rate': successful_requests / total_requests if total_requests > 0 else 0
        }
    
    def calculate_metrics(self):
        """Calculate metrics from results"""
        latencies = sorted(self.results['latencies'])
        
        if not latencies:
            return {}
        
        metrics = {
            'p50_latency_ms': latencies[len(latencies) // 2],
            'p95_latency_ms': latencies[int(len(latencies) * 0.95)],
            'p99_latency_ms': latencies[int(len(latencies) * 0.99)],
            'mean_latency_ms': statistics.mean(latencies),
            'stddev_latency_ms': statistics.stdev(latencies) if len(latencies) > 1 else 0,
            'min_latency_ms': min(latencies),
            'max_latency_ms': max(latencies),
        }
        
        statuses = self.results['statuses']
        metrics['error_rate'] = sum(1 for s in statuses if s != 200) / len(statuses) if statuses else 0
        
        return metrics


def main():
    parser = argparse.ArgumentParser(description='Load Generator for Control Plane')
    parser.add_argument('--base-url', default='http://localhost:8080', help='Control plane URL')
    parser.add_argument('--strategy', default='random', choices=['random', 'sticky'], help='Routing strategy')
    parser.add_argument('--rps', type=int, default=100, help='Requests per second')
    parser.add_argument('--duration', type=int, default=300, help='Duration in seconds')
    parser.add_argument('--sessions', type=int, default=1000, help='Number of sessions')
    parser.add_argument('--prompt-tokens', type=int, default=512, help='Prompt tokens')
    parser.add_argument('--max-tokens', type=int, default=256, help='Max tokens')
    parser.add_argument('--output', required=True, help='Output JSON file')
    
    args = parser.parse_args()
    
    generator = LoadGenerator(args.base_url, args.strategy)
    
    stats = generator.run_workload(
        rps=args.rps,
        duration=args.duration,
        sessions=args.sessions,
        prompt_tokens=args.prompt_tokens,
        max_tokens=args.max_tokens
    )
    
    metrics = generator.calculate_metrics()
    
    result = {
        'experiment': {
            'strategy': args.strategy,
            'rps': args.rps,
            'duration': args.duration,
            'sessions': args.sessions,
        },
        'stats': stats,
        'metrics': metrics,
        'timestamp': datetime.now().isoformat()
    }
    
    with open(args.output, 'w') as f:
        json.dump(result, f, indent=2)
    
    print(f"\nResults saved to {args.output}")
    print(f"\nMetrics:")
    for key, value in metrics.items():
        print(f"  {key}: {value:.2f}" if isinstance(value, float) else f"  {key}: {value}")


if __name__ == '__main__':
    main()

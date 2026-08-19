Must haves:
1. OTA with A/B + health-gated auto-rollback. "Un-brickable".
2. Autonomous watchdog + power-cycle.
3. Power telemetry with trending and alerts.
4. Remote console / config passthrough(tunnel to the MeshCore CLI over a UART link). Centralized config management?
5. Continuous RF health stats + noisescope on demand(periodic noise floor samples, neighbor RSSI/SNR, packet/forward counts, airtime from normal MeshCore operation)

Should have:

6. Boot-loop detection + log/crash capture
7. Telemetry store-and-forward
8. LTE self-monitoring
9. Enclosure environment monitoring

Nice to have:

10. Remote promiscuous packet capture, maybe some sort of MQTT server

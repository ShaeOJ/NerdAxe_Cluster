export interface IClusterSlave {
  id: number;
  hostname: string;
  ip_addr: string;
  hashrate: number;
  temp: number;
  fan_rpm: number;
  shares_accepted: number;
  shares_rejected: number;
  shares_submitted: number;
  frequency: number;
  core_voltage: number;
  power: number;
  voltage_in: number;
  state?: string;      // "online" | "warning" | "offline"
  last_seen?: number;  // ms since last heartbeat
}

export interface IClusterMaster {
  hostname: string;
  hashrate: number;
  temp: number;
  fan_rpm: number;
  frequency: number;
  core_voltage: number;
  power: number;
  voltage_in: number;
}

export interface IWatchdogDevice {
  is_throttled: boolean;
  is_recovering: boolean;
  throttle_reason: number;   // 0=none, 1=temp, 2=vin, 3=both
  last_temp: number;
  last_vin: number;
  current_frequency: number;
  current_voltage: number;
  original_frequency: number;
  original_voltage: number;
  throttle_count: number;
}

export interface IWatchdogSlave extends IWatchdogDevice {
  slot: number;
  slave_id: number;
  hostname: string;
}

export interface IWatchdogStatus {
  enabled: boolean;
  running: boolean;
  throttled_count: number;
  master: IWatchdogDevice;
  slaves: IWatchdogSlave[];
}

export interface IClusterStatus {
  mode: string;           // "disabled" | "master" | "slave"
  clusterMode: number;    // 0, 1, 2
  clusterChannel: number; // 1-14
  // Master fields
  active_slaves?: number;
  total_hashrate?: number;
  slaves?: IClusterSlave[];
  master?: IClusterMaster;
  total_power?: number;
  bestDiff?: number;     // cluster-wide best difficulty
  transport?: string;    // "espnow"
  // Slave fields
  registered?: boolean;
  slave_id?: number;
  shares_submitted?: number;
}

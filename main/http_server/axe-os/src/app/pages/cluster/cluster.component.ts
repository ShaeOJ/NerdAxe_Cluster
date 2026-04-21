import { Component, OnDestroy, OnInit } from '@angular/core';
import { SystemService } from 'src/app/services/system.service';
import { NbToastrService } from '@nebular/theme';
import { IClusterSlave, IClusterStatus, IWatchdogStatus } from '../../models/IClusterStatus';
import { catchError, of } from 'rxjs';

interface SlaveConfigForm {
  frequency: number;
  coreVoltage: number;
  fanSpeed: number;
  fanMode: number;
  targetTemp: number;
}

@Component({
  selector: 'app-cluster',
  templateUrl: './cluster.component.html',
  styleUrls: ['./cluster.component.scss']
})
export class ClusterComponent implements OnInit, OnDestroy {

  public clusterStatus: IClusterStatus | null = null;
  public watchdogStatus: IWatchdogStatus | null = null;
  public watchdogTogglingEnabled = false;
  public loading = true;

  public editMode: number = 0;
  public editChannel: number = 1;

  public pendingRestart = false;

  // Per-slave config panel state
  public expandedSlaveId: number | null = null;
  public slaveConfigs: { [id: number]: SlaveConfigForm } = {};
  public savingSlaveId: number | null = null;
  public restartingSlaveId: number | null = null;

  private pollRef!: number;

  constructor(
    private systemService: SystemService,
    private toastrService: NbToastrService,
  ) {}

  ngOnInit(): void {
    this.fetchStatus();
    this.pollRef = window.setInterval(() => {
      this.fetchStatus();
    }, 3000);
  }

  ngOnDestroy(): void {
    window.clearInterval(this.pollRef);
  }

  private fetchStatus(): void {
    this.systemService.getSwarmInfo().pipe(
      catchError(() => {
        this.toastrService.danger('Failed to fetch cluster status', 'Error');
        return of(null);
      })
    ).subscribe((res: any) => {
      if (res) {
        this.clusterStatus = res as IClusterStatus;
        if (this.loading) {
          this.editMode = this.clusterStatus.clusterMode;
          this.editChannel = this.clusterStatus.clusterChannel;
        }
        this.loading = false;
      }
    });

    if (this.clusterStatus?.mode === 'master' || !this.clusterStatus) {
      this.systemService.getWatchdogStatus().pipe(
        catchError(() => of(null))
      ).subscribe((res: any) => {
        if (res) this.watchdogStatus = res as IWatchdogStatus;
      });
    }
  }

  public toggleWatchdog(): void {
    if (!this.watchdogStatus || this.watchdogTogglingEnabled) return;
    const newEnabled = !this.watchdogStatus.enabled;
    this.watchdogTogglingEnabled = true;
    this.systemService.setWatchdogEnabled(newEnabled).pipe(
      catchError(() => {
        this.toastrService.danger('Failed to update watchdog', 'Error');
        return of(null);
      })
    ).subscribe((res) => {
      this.watchdogTogglingEnabled = false;
      if (res !== null) {
        if (this.watchdogStatus) this.watchdogStatus.enabled = newEnabled;
        this.toastrService.success(`Watchdog ${newEnabled ? 'enabled' : 'disabled'}`, 'Success');
      }
    });
  }

  public getThrottleReasonLabel(reason: number): string {
    if (reason === 0) return '';
    const parts: string[] = [];
    if (reason & 1) parts.push('TEMP');
    if (reason & 2) parts.push('VIN');
    return parts.join('+');
  }

  public saveConfig(): void {
    const payload = {
      clusterMode: this.editMode,
      clusterChannel: this.editChannel,
    };

    this.systemService.updateSwarm('', payload).pipe(
      catchError((err) => {
        this.toastrService.danger('Failed to save cluster config', 'Error');
        return of(null);
      })
    ).subscribe((res) => {
      if (res !== null) {
        this.toastrService.success('Cluster configuration saved', 'Success');
        this.pendingRestart = true;
      }
    });
  }

  public restart(): void {
    this.systemService.restart().pipe(
      catchError(() => {
        this.toastrService.danger('Failed to restart device', 'Error');
        return of(null);
      })
    ).subscribe((res) => {
      if (res !== null) {
        this.toastrService.success('Device restarting...', 'Success');
        this.pendingRestart = false;
      }
    });
  }

  public toggleSlaveConfig(slave: IClusterSlave): void {
    if (this.expandedSlaveId === slave.id) {
      this.expandedSlaveId = null;
    } else {
      this.expandedSlaveId = slave.id;
      if (!this.slaveConfigs[slave.id]) {
        this.slaveConfigs[slave.id] = {
          frequency: slave.frequency || 485,
          coreVoltage: slave.core_voltage || 1200,
          fanSpeed: 100,
          fanMode: 0,
          targetTemp: 55,
        };
      }
    }
  }

  public saveSlaveConfig(slave: IClusterSlave): void {
    const cfg = this.slaveConfigs[slave.id];
    if (!cfg) return;
    this.savingSlaveId = slave.id;
    const payload = {
      frequency: cfg.frequency,
      coreVoltage: cfg.coreVoltage,
      fanSpeed: cfg.fanSpeed,
      fanMode: cfg.fanMode,
      targetTemp: cfg.targetTemp,
    };
    this.systemService.updateSlave(slave.id, payload).pipe(
      catchError(() => {
        this.toastrService.danger(`Failed to configure slave #${slave.id}`, 'Error');
        return of(null);
      })
    ).subscribe((res) => {
      this.savingSlaveId = null;
      if (res !== null) {
        this.toastrService.success(`Config applied to slave #${slave.id}`, 'Success');
        this.expandedSlaveId = null;
      }
    });
  }

  public restartSlave(slave: IClusterSlave): void {
    this.restartingSlaveId = slave.id;
    this.systemService.restartSlave(slave.id).pipe(
      catchError(() => {
        this.toastrService.danger(`Failed to restart slave #${slave.id}`, 'Error');
        return of(null);
      })
    ).subscribe((res) => {
      this.restartingSlaveId = null;
      if (res !== null) {
        this.toastrService.success(`Slave #${slave.id} restarting...`, 'Success');
      }
    });
  }

  public getStateStatus(state?: string): string {
    if (state === 'online') return 'success';
    if (state === 'warning') return 'warning';
    return 'danger';
  }

  public formatLastSeen(lastSeenMs?: number): string {
    if (lastSeenMs === undefined || lastSeenMs === null) return '';
    const s = Math.floor(lastSeenMs / 1000);
    if (s < 60) return `${s}s ago`;
    return `${Math.floor(s / 60)}m ago`;
  }

  public getTempClass(temp: number): string {
    if (temp > 70) return 'temp-red';
    if (temp > 60) return 'temp-orange';
    return 'temp-green';
  }

  public getTotalShares(): { accepted: number; rejected: number } {
    if (!this.clusterStatus?.slaves) return { accepted: 0, rejected: 0 };
    return this.clusterStatus.slaves.reduce(
      (acc, s) => ({
        accepted: acc.accepted + s.shares_accepted,
        rejected: acc.rejected + s.shares_rejected,
      }),
      { accepted: 0, rejected: 0 }
    );
  }

  public formatHashrate(gh: number): string {
    if (gh >= 1000) {
      return (gh / 1000).toFixed(2) + ' TH/s';
    }
    return gh.toFixed(2) + ' GH/s';
  }

  public formatDiff(diff?: number): string {
    if (!diff || diff === 0) return '—';
    if (diff >= 1e12) return (diff / 1e12).toFixed(2) + 'T';
    if (diff >= 1e9) return (diff / 1e9).toFixed(2) + 'G';
    if (diff >= 1e6) return (diff / 1e6).toFixed(2) + 'M';
    if (diff >= 1e3) return (diff / 1e3).toFixed(2) + 'K';
    return diff.toFixed(0);
  }

  public getTotalPower(): number {
    return this.clusterStatus?.total_power || 0;
  }

  public getClusterEfficiency(): string {
    const power = this.clusterStatus?.total_power || 0;
    const hashrate = this.clusterStatus?.total_hashrate || 0;
    if (hashrate <= 0 || power <= 0) return '—';
    // J/TH = W / (GH/s) * 1000
    return (power / hashrate * 1000).toFixed(2) + ' J/TH';
  }

  public getDeviceEfficiency(power: number, hashrate: number): string {
    if (hashrate <= 0 || power <= 0) return '—';
    return (power / hashrate * 1000).toFixed(2) + ' J/TH';
  }
}

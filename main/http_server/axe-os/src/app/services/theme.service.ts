import { Injectable } from '@angular/core';
import { BehaviorSubject } from 'rxjs';

export interface ThemeSettings {
  colorScheme: string;     // 'cosmic' | 'default' | 'dark'
  accentColor: string;     // primary hex color e.g. '#14F593'
  accentName: string;      // e.g. 'Green'
  pipboyEffects: boolean;  // CRT scanlines + glow
  pipboyFont: boolean;     // Nippo font
}

export interface AccentColorOption {
  name: string;
  hex: string;
}

const STORAGE_KEY = 'themeSettings';

const DEFAULT_SETTINGS: ThemeSettings = {
  colorScheme: 'dark',
  accentColor: '#14F593',
  accentName: 'Green',
  pipboyEffects: true,
  pipboyFont: true,
};

@Injectable({
  providedIn: 'root'
})
export class ThemeSettingsService {

  static readonly ACCENT_COLORS: AccentColorOption[] = [
    { name: 'Green',    hex: '#14F593' },
    { name: 'Amber',    hex: '#FFB000' },
    { name: 'Blue',     hex: '#00D4FF' },
    { name: 'Orange',   hex: '#FF8C00' },
    { name: 'Pink',     hex: '#FF6EC7' },
    { name: 'Red',      hex: '#FF3F3F' },
    { name: 'Vault-Tec', hex: '#FFCC00' },
    { name: 'Purple',   hex: '#A855F7' },
    { name: 'Teal',     hex: '#14B8A6' },
    { name: 'White',    hex: '#E0E0E0' },
  ];

  private settings$ = new BehaviorSubject<ThemeSettings>(DEFAULT_SETTINGS);
  readonly themeSettings$ = this.settings$.asObservable();

  constructor() {
    this.loadFromStorage();
    this.applyAll();
  }

  get currentSettings(): ThemeSettings {
    return this.settings$.value;
  }

  updateColorScheme(scheme: string): void {
    this.update({ colorScheme: scheme });
  }

  updateAccentColor(hex: string, name: string): void {
    this.update({ accentColor: hex, accentName: name });
  }

  updatePipboyEffects(enabled: boolean): void {
    this.update({ pipboyEffects: enabled });
  }

  updatePipboyFont(enabled: boolean): void {
    this.update({ pipboyFont: enabled });
  }

  private update(partial: Partial<ThemeSettings>): void {
    const next = { ...this.settings$.value, ...partial };
    this.settings$.next(next);
    this.saveToStorage(next);
    this.applyAll();
  }

  private loadFromStorage(): void {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const parsed = JSON.parse(raw) as Partial<ThemeSettings>;
        this.settings$.next({ ...DEFAULT_SETTINGS, ...parsed });
      }
    } catch {
      // Corrupt data — use defaults
    }
  }

  private saveToStorage(settings: ThemeSettings): void {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
  }

  /** Apply all visual settings to the DOM */
  private applyAll(): void {
    const s = this.settings$.value;
    this.applyAccentColor(s.accentColor);
    this.applyBodyClasses(s);
  }

  /** Set CSS custom properties for the accent color on :root */
  private applyAccentColor(hex: string): void {
    const root = document.documentElement;
    const rgb = this.hexToRgb(hex);

    root.style.setProperty('--pipboy-accent', hex);
    root.style.setProperty('--pipboy-accent-rgb', `${rgb.r}, ${rgb.g}, ${rgb.b}`);
    root.style.setProperty('--pipboy-accent-hover', this.darken(hex, 0.15));
  }

  /** Toggle body classes for effects and font */
  private applyBodyClasses(s: ThemeSettings): void {
    document.body.classList.toggle('pipboy-effects', s.pipboyEffects);
    document.body.classList.toggle('pipboy-font', s.pipboyFont);
    document.body.classList.toggle('pipboy-theme-light', s.colorScheme === 'default');
  }

  private hexToRgb(hex: string): { r: number; g: number; b: number } {
    const h = hex.replace('#', '');
    return {
      r: parseInt(h.substring(0, 2), 16),
      g: parseInt(h.substring(2, 4), 16),
      b: parseInt(h.substring(4, 6), 16),
    };
  }

  /** Darken a hex color by a fraction (0–1) */
  private darken(hex: string, amount: number): string {
    const rgb = this.hexToRgb(hex);
    const factor = 1 - amount;
    const r = Math.round(rgb.r * factor);
    const g = Math.round(rgb.g * factor);
    const b = Math.round(rgb.b * factor);
    return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;
  }
}

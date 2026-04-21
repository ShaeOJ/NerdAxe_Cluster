import { Component, OnInit, OnDestroy } from '@angular/core';
import { NbThemeService } from '@nebular/theme';
import { Subject } from 'rxjs';
import { takeUntil } from 'rxjs/operators';
import { ThemeSettingsService, ThemeSettings, AccentColorOption } from '../../services/theme.service';

@Component({
  selector: 'ngx-design',
  templateUrl: './design.component.html',
  styleUrls: ['./design.component.scss'],
})
export class DesignComponent implements OnInit, OnDestroy {

  private destroy$ = new Subject<void>();

  colorSchemes = [
    { value: 'cosmic', i18nKey: 'DESIGN.COSMIC' },
    { value: 'default', i18nKey: 'DESIGN.LIGHT' },
    { value: 'dark', i18nKey: 'DESIGN.DARK' },
  ];

  accentColors: AccentColorOption[] = ThemeSettingsService.ACCENT_COLORS;

  settings: ThemeSettings;

  constructor(
    private themeService: NbThemeService,
    private themeSettings: ThemeSettingsService,
  ) {
    this.settings = this.themeSettings.currentSettings;
  }

  ngOnInit(): void {
    this.themeSettings.themeSettings$
      .pipe(takeUntil(this.destroy$))
      .subscribe(s => this.settings = s);
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  selectColorScheme(scheme: string): void {
    this.themeService.changeTheme(scheme);
    this.themeSettings.updateColorScheme(scheme);
    // Also persist for the header's existing localStorage key
    localStorage.setItem('selectedTheme', scheme);
  }

  selectAccentColor(color: AccentColorOption): void {
    this.themeSettings.updateAccentColor(color.hex, color.name);
  }

  toggleEffects(enabled: boolean): void {
    this.themeSettings.updatePipboyEffects(enabled);
  }

  toggleFont(enabled: boolean): void {
    this.themeSettings.updatePipboyFont(enabled);
  }
}

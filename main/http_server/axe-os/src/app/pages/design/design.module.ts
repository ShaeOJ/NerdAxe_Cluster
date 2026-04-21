import { NgModule } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { NbCardModule, NbButtonModule, NbIconModule, NbToggleModule, NbRadioModule } from '@nebular/theme';
import { DesignComponent } from './design.component';
import { I18nModule } from '../../@i18n/i18n.module';

@NgModule({
  declarations: [
    DesignComponent,
  ],
  imports: [
    CommonModule,
    FormsModule,
    NbCardModule,
    NbButtonModule,
    NbIconModule,
    NbToggleModule,
    NbRadioModule,
    I18nModule,
  ],
  exports: [
    DesignComponent,
  ]
})
export class DesignModule { }

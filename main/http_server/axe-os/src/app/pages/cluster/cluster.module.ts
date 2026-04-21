import { NgModule } from '@angular/core';
import { CommonModule } from '@angular/common';
import { ReactiveFormsModule, FormsModule } from '@angular/forms';
import { NbCardModule, NbButtonModule, NbIconModule, NbSelectModule, NbInputModule, NbTooltipModule, NbBadgeModule, NbSpinnerModule } from '@nebular/theme';
import { ClusterComponent } from './cluster.component';
import { PipesModule } from '../../pipes/pipes.module';
import { I18nModule } from '../../@i18n/i18n.module';

@NgModule({
  declarations: [
    ClusterComponent,
  ],
  imports: [
    CommonModule,
    ReactiveFormsModule,
    FormsModule,
    NbCardModule,
    NbButtonModule,
    NbIconModule,
    NbSelectModule,
    NbInputModule,
    NbTooltipModule,
    NbBadgeModule,
    NbSpinnerModule,
    PipesModule,
    I18nModule
  ],
  exports: [
    ClusterComponent
  ]
})
export class ClusterModule { }

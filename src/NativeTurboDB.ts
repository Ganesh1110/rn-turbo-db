import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * TurboModule spec for the native JSI installer.
 * This module is only used to invoke install() once at app startup
 * and to get the documents directory path.
 *
 * The actual DB operations are exposed directly on global.NativeDB
 * by the C++ JSI host object (DBEngine), AFTER install() is called.
 */
export interface Spec extends TurboModule {
  /**
   * Returns true if JSI installation was successful.
   * Marking return type as boolean helps Codegen/TurboModule handle it more reliably.
   */
  install(): boolean;
  getDocumentsDirectory(): string;
  isInitialized(): boolean;
}

export default TurboModuleRegistry.get<Spec>('TurboDB');

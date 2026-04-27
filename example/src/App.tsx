import { useState, useMemo, useEffect, useCallback } from 'react';
import {
  SafeAreaView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
  ScrollView,
  TextInput,
  Alert,
  ActivityIndicator,
  Platform,
  LayoutAnimation,
  UIManager,
} from 'react-native';
import { TurboDB } from 'rn-turbo-db';
import { SyncPage } from './SyncPage';

if (
  Platform.OS === 'android' &&
  UIManager.setLayoutAnimationEnabledExperimental
) {
  UIManager.setLayoutAnimationEnabledExperimental(true);
}

const BenchmarkPage = () => {
  const [results, setResults] = useState<number[]>([]);
  const [isRunning, setIsRunning] = useState(false);

  const NUM_OPERATIONS = 1000;
  const NUM_QUERIES = 2000;

  const runBenchmark = async () => {
    try {
      setIsRunning(true);
      LayoutAnimation.configureNext(LayoutAnimation.Presets.easeInEaseOut);
      setResults([]);

      const docsDir = TurboDB.getDocumentsDirectory();
      const benchPath = `${docsDir}/bench_turbo_standalone.db`;
      // Use async factory to guarantee initStorage before any operation
      const secureDB = await TurboDB.create(benchPath, 20 * 1024 * 1024);
      await secureDB.deleteAllAsync();

      const entries: Record<string, any> = {};
      for (let i = 0; i < NUM_OPERATIONS; i++) {
        entries[`key_${i}`] = {
          id: i,
          data: Math.random().toString(36),
          val: Math.random(),
        };
      }

      const times: number[] = [];

      const insertStart = Date.now();
      await secureDB.setMultiAsync(entries); // async — non-blocking
      times.push(Date.now() - insertStart);

      const readStart = Date.now();
      for (let i = 0; i < NUM_QUERIES; i++) {
        await secureDB.getAsync(
          `key_${Math.floor(Math.random() * NUM_OPERATIONS)}`
        );
      }
      times.push(Date.now() - readStart);

      const rangeStart = Date.now();
      await secureDB.rangeQueryAsync('key_100', 'key_300');
      times.push(Date.now() - rangeStart);

      const deleteStart = Date.now();
      await secureDB.deleteAllAsync();
      times.push(Date.now() - deleteStart);

      LayoutAnimation.configureNext(LayoutAnimation.Presets.spring);
      setResults(times);
    } catch (e) {
      console.error('Benchmark Error:', e);
      Alert.alert('Benchmark Failed', String(e));
    } finally {
      setIsRunning(false);
    }
  };

  const formatTime = (ms: number | undefined) =>
    ms === undefined ? '-' : `${ms}ms`;

  const maxVal = useMemo(() => Math.max(...results, 1), [results]);

  const Row = ({ label, idx }: { label: string; idx: number }) => {
    const val = results[idx] || 0;
    const widthPercent = results.length > 0 ? (val / maxVal) * 100 : 0;

    return (
      <View style={styles.resultRowContainer}>
        <View style={styles.resultLabelRow}>
          <Text style={styles.resultLabel}>{label}</Text>
          <Text style={[styles.resultValue, styles.secureDBColor]}>
            {formatTime(results[idx])}
          </Text>
        </View>
        <View style={styles.barBackground}>
          <View
            style={[
              styles.barForeground,
              { width: `${Math.max(widthPercent, 2)}%` },
            ]}
          />
        </View>
      </View>
    );
  };

  return (
    <View style={styles.benchmarkCard}>
      <View style={styles.benchmarkHeader}>
        <Text style={styles.cardTitle}>Performance Analytics</Text>
        <Text style={styles.benchmarkSubtitle}>
          {NUM_OPERATIONS} ops | {NUM_QUERIES} queries
        </Text>
      </View>

      <TouchableOpacity
        style={[
          styles.benchmarkButton,
          isRunning && styles.benchmarkButtonDisabled,
        ]}
        onPress={runBenchmark}
        disabled={isRunning}
      >
        {isRunning ? (
          <ActivityIndicator size="small" color="#fff" />
        ) : (
          <Text style={styles.benchmarkButtonText}>⚡ Start Native Engine</Text>
        )}
      </TouchableOpacity>

      <View style={styles.resultTable}>
        <Row label="Bulk Atomic Insert" idx={0} />
        <Row label="Random JSI Read" idx={1} />
        <Row label="B-Tree Range Query" idx={2} />
        <Row label="Storage Purge" idx={3} />
      </View>

      {results.length > 0 && (
        <View style={styles.winnerBanner}>
          <Text style={styles.winnerText}>
            ⚡ Turbo Mode Active: Total{' '}
            {results.reduce((a, b) => a + b, 0).toFixed(0)}ms latency
          </Text>
        </View>
      )}
    </View>
  );
};

export default function App() {
  const [activeTab, setActiveTab] = useState<'demo' | 'benchmark' | 'sync'>(
    'demo'
  );
  const [key, setKey] = useState('');
  const [value, setValue] = useState('');
  const [dbPath, setDbPath] = useState('');
  const [allKeys, setAllKeys] = useState<string[]>([]);
  const [getResult, setGetResult] = useState<string>('');
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [statusMessage, setStatusMessage] = useState<string>('');
  const [statusType, setStatusType] = useState<'success' | 'error' | 'info'>(
    'info'
  );
  const [rangeStart, setRangeStart] = useState('a');
  const [rangeEnd, setRangeEnd] = useState('z');

  const [db, setDb] = useState<TurboDB | null>(null);

  const refreshKeys = useCallback(() => {
    if (!db) return;
    try {
      LayoutAnimation.configureNext(LayoutAnimation.Presets.easeInEaseOut);
      setAllKeys(db.getAllKeys());
    } catch (e) {
      console.error('Refresh Keys Error:', e);
    }
  }, [db]);

  useEffect(() => {
    const initDB = async () => {
      try {
        // In Fabric/Bridgeless mode, a short delay before the very first JSI call
        // can help ensure the native modules are fully wired up.
        await new Promise((r) => setTimeout(r, 1000));

        const docPath = TurboDB.getDocumentsDirectory();
        setDbPath(docPath);
        const dbFile = `${docPath}/secure_v1.db`;

        // TurboDB.create() internally handles JSI installation and storage init
        const newDb = await TurboDB.create(dbFile, 10 * 1024 * 1024, {
          syncEnabled: true,
        });
        setDb(newDb);

        setStatusType('success');
        setStatusMessage('🚀 TurboDB Engine Ready');
        setTimeout(() => setStatusMessage(''), 3000);
      } catch (e) {
        console.error('[App] DB initialization failed:', e);
        setStatusType('error');
        setStatusMessage(`Init Failed: ${String(e)}`);
        Alert.alert('DB Error', `Failed to initialize database: ${String(e)}`);
      }
    };
    initDB();
  }, []);

  useEffect(() => {
    if (db) {
      refreshKeys();
    }
  }, [db, refreshKeys]);

  const handleSet = () => {
    if (!key) return Alert.alert('Error', 'Please enter a key');
    if (!db) {
      setStatusType('error');
      setStatusMessage('Database not ready yet. Please wait.');
      return;
    }
    try {
      const data =
        value.startsWith('{') || value.startsWith('[')
          ? JSON.parse(value)
          : value;
      const success = db?.set(key, data);
      if (success) {
        setKey('');
        setValue('');
        setGetResult('');
        refreshKeys();
        setStatusType('success');
        setStatusMessage(`💾 Saved: ${key}`);
        setTimeout(() => setStatusMessage(''), 2000);
      } else {
        setStatusType('error');
        setStatusMessage('Failed to save key');
      }
    } catch (e) {
      setStatusType('error');
      setStatusMessage(`Invalid value: ${String(e)}`);
      setTimeout(() => setStatusMessage(''), 3000);
    }
  };

  const handleGet = () => {
    if (!db) return;
    if (!key) {
      setStatusType('error');
      setStatusMessage('Please enter a key');
      return;
    }
    const res = db.get(key);
    if (res === undefined) {
      setGetResult('NOT FOUND');
      setStatusType('error');
      setStatusMessage(`Key not found: ${key}`);
    } else {
      setGetResult(JSON.stringify(res, null, 2));
      setStatusType('info');
      setStatusMessage(`🔍 Found: ${key}`);
      setTimeout(() => setStatusMessage(''), 2000);
    }
  };

  const handleDel = (targetKey?: string) => {
    const k = targetKey || key;
    if (!k) {
      setStatusType('error');
      setStatusMessage('Please enter a key');
      return;
    }
    if (!db) {
      setStatusType('error');
      setStatusMessage('Database not initialized');
      return;
    }
    Alert.alert('Delete Key', `Are you sure you want to delete "${k}"?`, [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Delete',
        style: 'destructive',
        onPress: () => {
          try {
            const result = db.del(k);
            if (result) {
              if (k === key) {
                setGetResult('');
                setKey('');
              }
              refreshKeys();
              setStatusType('success');
              setStatusMessage(`🗑️ Deleted: ${k}`);
              setTimeout(() => setStatusMessage(''), 2000);
            } else {
              setStatusType('error');
              setStatusMessage(`Key not found: ${k}`);
            }
          } catch (e) {
            setStatusType('error');
            setStatusMessage(`Error: ${String(e)}`);
            setTimeout(() => setStatusMessage(''), 3000);
          }
        },
      },
    ]);
  };

  const handleRange = () => {
    if (!db) {
      setStatusType('error');
      setStatusMessage('Database not ready yet. Please wait.');
      return;
    }
    if (!rangeStart || !rangeEnd) {
      Alert.alert('Error', 'Please enter start and end keys');
      return;
    }
    try {
      const results = db.rangeQuery(rangeStart, rangeEnd);
      Alert.alert(
        'Range Query',
        `Found ${results.length} item${results.length !== 1 ? 's' : ''} between "${rangeStart}" and "${rangeEnd}"`,
        [
          { text: 'OK' },
          {
            text: 'View Details',
            onPress: () => setGetResult(JSON.stringify(results, null, 2)),
          },
        ]
      );
    } catch (e) {
      setStatusType('error');
      setStatusMessage(`Range query failed: ${String(e)}`);
    }
  };

  const handleTurboInsert = async () => {
    if (!db) return;
    const start = Date.now();
    const batch: Record<string, any> = {};
    for (let i = 0; i < 500; i++) {
      batch[`turbo_${i}`] = { id: i, ts: Date.now() };
    }
    await db.setMultiAsync(batch); // async — non-blocking
    const elapsed = Date.now() - start;
    refreshKeys();
    Alert.alert('Turbo Success', `Inserted 500 records in ${elapsed}ms`);
  };

  return (
    <SafeAreaView style={styles.container}>
      <ScrollView contentContainerStyle={styles.scrollContent}>
        <View style={styles.header}>
          <Text style={styles.title}>TurboDB</Text>
          <Text style={styles.subtitle}>Unified Crypto + JSI B-Tree</Text>
        </View>

        <View style={styles.tabContainer}>
          {['demo', 'benchmark', 'sync'].map((tab) => (
            <TouchableOpacity
              key={tab}
              style={[styles.tab, activeTab === tab && styles.activeTab]}
              onPress={() => {
                LayoutAnimation.configureNext(
                  LayoutAnimation.Presets.easeInEaseOut
                );
                setActiveTab(tab as any);
              }}
            >
              <Text
                style={[
                  styles.tabText,
                  activeTab === tab && styles.activeTabText,
                ]}
              >
                {tab.toUpperCase()}
              </Text>
            </TouchableOpacity>
          ))}
        </View>

        {activeTab === 'demo' ? (
          <>
            <View style={styles.metaCard}>
              <Text style={styles.metaLabel}>
                ACTIVE REALM: {Platform.OS.toUpperCase()}
              </Text>
              <Text style={styles.metaValue} numberOfLines={1}>
                {dbPath}
              </Text>
            </View>

            {statusMessage ? (
              <View
                style={[
                  styles.statusBanner,
                  statusType === 'success' && styles.statusSuccess,
                  statusType === 'error' && styles.statusError,
                  statusType === 'info' && styles.statusInfo,
                ]}
              >
                <Text
                  style={[
                    styles.statusText,
                    statusType === 'error' && styles.statusTextError,
                  ]}
                >
                  {statusMessage}
                </Text>
                <TouchableOpacity onPress={() => setStatusMessage('')}>
                  <Text style={styles.statusClose}>×</Text>
                </TouchableOpacity>
              </View>
            ) : null}

            <View style={styles.card}>
              <Text style={styles.cardTitle}>Rapid I/O Interface</Text>
              <TextInput
                style={styles.input}
                value={key}
                onChangeText={setKey}
                placeholder="Entry key..."
                placeholderTextColor="#475569"
              />
              <TextInput
                style={[styles.input, { height: 80 }]}
                value={value}
                onChangeText={setValue}
                placeholder="Payload (Text or JSON)..."
                placeholderTextColor="#475569"
                multiline
              />

              <View style={styles.buttonRow}>
                <TouchableOpacity style={styles.button} onPress={handleSet}>
                  <Text style={styles.buttonText}>💾 SET</Text>
                </TouchableOpacity>
                <TouchableOpacity
                  style={[styles.button, { backgroundColor: '#3B82F6' }]}
                  onPress={handleGet}
                >
                  <Text style={[styles.buttonText, { color: '#fff' }]}>
                    🔍 GET
                  </Text>
                </TouchableOpacity>
                <TouchableOpacity
                  style={[styles.button, { backgroundColor: '#F59E0B' }]}
                  onPress={() => handleDel()}
                >
                  <Text style={[styles.buttonText, { color: '#fff' }]}>
                    🗑️ DEL
                  </Text>
                </TouchableOpacity>
              </View>
            </View>

            {getResult !== '' && (
              <View style={styles.resultCard}>
                <View style={styles.rowBetween}>
                  <Text style={styles.resultLabel}>Output Stream</Text>
                  <TouchableOpacity onPress={() => setGetResult('')}>
                    <Text style={styles.statusClose}>×</Text>
                  </TouchableOpacity>
                </View>
                <Text style={styles.resultText}>{getResult}</Text>
              </View>
            )}

            <View style={styles.card}>
              <TouchableOpacity
                style={styles.advancedToggle}
                onPress={() => {
                  LayoutAnimation.configureNext(
                    LayoutAnimation.Presets.easeInEaseOut
                  );
                  setShowAdvanced(!showAdvanced);
                }}
              >
                <Text style={styles.cardTitle}>Advanced Toolset</Text>
                <Text style={styles.toggleArrow}>
                  {showAdvanced ? '▼' : '▲'}
                </Text>
              </TouchableOpacity>

              {showAdvanced && (
                <View style={styles.advancedContent}>
                  <Text style={styles.inputLabel}>Range Scan</Text>
                  <View style={styles.buttonRow}>
                    <TextInput
                      style={[styles.input, { flex: 1, marginBottom: 0 }]}
                      value={rangeStart}
                      onChangeText={setRangeStart}
                      placeholder="Min"
                    />
                    <TextInput
                      style={[styles.input, { flex: 1, marginBottom: 0 }]}
                      value={rangeEnd}
                      onChangeText={setRangeEnd}
                      placeholder="Max"
                    />
                  </View>
                  <TouchableOpacity
                    style={styles.advancedButton}
                    onPress={handleRange}
                  >
                    <Text style={styles.advancedButtonText}>
                      🔭 B-Tree Scan
                    </Text>
                  </TouchableOpacity>
                  <TouchableOpacity
                    style={[styles.advancedButton, { borderColor: '#10B981' }]}
                    onPress={handleTurboInsert}
                  >
                    <Text
                      style={[styles.advancedButtonText, { color: '#10B981' }]}
                    >
                      🚀 Bulk Stress Test (500)
                    </Text>
                  </TouchableOpacity>
                  <TouchableOpacity
                    style={[styles.advancedButton, { borderColor: '#EF4444' }]}
                    onPress={async () => {
                      await db?.deleteAllAsync();
                      refreshKeys();
                      setStatusMessage('💥 Database Wiped');
                      setTimeout(() => setStatusMessage(''), 2000);
                    }}
                  >
                    <Text
                      style={[styles.advancedButtonText, { color: '#EF4444' }]}
                    >
                      ☢️ Wipe Storage
                    </Text>
                  </TouchableOpacity>
                </View>
              )}
            </View>

            <View style={styles.card}>
              <View style={styles.rowBetween}>
                <Text style={styles.cardTitle}>
                  Live Index ({allKeys.length})
                </Text>
                <TouchableOpacity onPress={refreshKeys}>
                  <Text style={styles.refreshText}>↻ Refresh</Text>
                </TouchableOpacity>
              </View>
              <View style={styles.keyList}>
                {allKeys.length === 0 ? (
                  <Text style={styles.emptyText}>Index is currently void.</Text>
                ) : (
                  allKeys.slice(0, 15).map((k) => (
                    <View key={k} style={styles.keyItem}>
                      <TouchableOpacity
                        onPress={() => {
                          // APP-F fix: read value directly with the captured key `k`
                          // (do NOT use setKey+handleGet — React state is async and
                          //  handleGet would read the old `key` from the previous render)
                          if (!db) return;
                          const res = db.get(k);
                          setKey(k);
                          if (res === undefined) {
                            setGetResult('NOT FOUND');
                            setStatusType('error');
                            setStatusMessage(`Key not found: ${k}`);
                          } else {
                            setGetResult(JSON.stringify(res, null, 2));
                            setStatusType('info');
                            setStatusMessage(`🔍 Found: ${k}`);
                            setTimeout(() => setStatusMessage(''), 2000);
                          }
                        }}
                        style={{ flex: 1 }}
                      >
                        <Text style={styles.keyText}>
                          🔑 {k.length > 20 ? k.substring(0, 17) + '...' : k}
                        </Text>
                      </TouchableOpacity>
                      <View style={styles.keyActions}>
                        <TouchableOpacity
                          onPress={() => {
                            if (!db) return;
                            const res = db.get(k);
                            setKey(k);
                            if (res === undefined) {
                              setGetResult('NOT FOUND');
                            } else {
                              setGetResult(JSON.stringify(res, null, 2));
                              setStatusType('info');
                              setStatusMessage(`🔍 Found: ${k}`);
                              setTimeout(() => setStatusMessage(''), 2000);
                            }
                          }}
                        >
                          <Text style={styles.actionIcon}>🔍</Text>
                        </TouchableOpacity>
                        <TouchableOpacity
                          onPress={() => {
                            handleDel(k);
                          }}
                        >
                          <Text style={styles.actionIcon}>🗑️</Text>
                        </TouchableOpacity>
                      </View>
                    </View>
                  ))
                )}
                {allKeys.length > 15 && (
                  <Text style={styles.moreKeysText}>
                    + {allKeys.length - 15} entries not shown
                  </Text>
                )}
              </View>
            </View>
          </>
        ) : activeTab === 'benchmark' ? (
          <BenchmarkPage />
        ) : (
          db && <SyncPage db={db} />
        )}
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#050511' },
  scrollContent: { padding: 24, paddingBottom: 60 },
  header: { alignItems: 'center', marginVertical: 35 },
  title: {
    fontSize: 48,
    fontWeight: '900',
    color: '#FFFFFF',
    letterSpacing: -1,
    textShadowColor: '#6366f1',
    textShadowOffset: { width: 0, height: 0 },
    textShadowRadius: 20,
  },
  subtitle: {
    fontSize: 12,
    color: '#A5B4FC',
    fontWeight: '800',
    textTransform: 'uppercase',
    letterSpacing: 4,
    marginTop: 8,
    opacity: 0.8,
  },
  tabContainer: {
    flexDirection: 'row',
    backgroundColor: '#0A0A1F',
    borderRadius: 24,
    padding: 6,
    marginBottom: 30,
    borderWidth: 1,
    borderColor: 'rgba(99, 102, 241, 0.15)',
    shadowColor: '#6366f1',
    shadowOffset: { width: 0, height: 8 },
    shadowOpacity: 0.1,
    shadowRadius: 15,
  },
  tab: { flex: 1, paddingVertical: 14, alignItems: 'center', borderRadius: 18 },
  activeTab: {
    backgroundColor: '#1E1B4B',
    borderWidth: 1,
    borderColor: 'rgba(99, 102, 241, 0.4)',
    shadowColor: '#6366f1',
    shadowOffset: { width: 0, height: 0 },
    shadowOpacity: 0.5,
    shadowRadius: 10,
  },
  tabText: {
    color: '#64748B',
    fontSize: 12,
    fontWeight: '800',
    letterSpacing: 1,
  },
  activeTabText: { color: '#E0E7FF' },
  metaCard: {
    backgroundColor: '#0F0F28',
    borderRadius: 20,
    padding: 16,
    marginBottom: 20,
    borderLeftWidth: 4,
    borderLeftColor: '#8B5CF6',
    borderRightWidth: 1,
    borderTopWidth: 1,
    borderBottomWidth: 1,
    borderColor: 'rgba(139, 92, 246, 0.1)',
  },
  metaLabel: {
    color: '#475569',
    fontSize: 11,
    fontWeight: '900',
    letterSpacing: 1,
  },
  metaValue: {
    color: '#C4B5FD',
    fontSize: 12,
    marginTop: 6,
    fontFamily: Platform.OS === 'ios' ? 'Menlo' : 'monospace',
  },
  card: {
    backgroundColor: 'rgba(15, 15, 40, 0.6)',
    borderRadius: 24,
    padding: 24,
    borderWidth: 1,
    borderColor: 'rgba(99, 102, 241, 0.15)',
    marginBottom: 20,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 10 },
    shadowOpacity: 0.3,
    shadowRadius: 20,
  },
  cardTitle: {
    color: '#F8FAFC',
    fontSize: 18,
    fontWeight: '800',
    marginBottom: 20,
    letterSpacing: -0.5,
  },
  input: {
    backgroundColor: '#050511',
    borderRadius: 16,
    padding: 18,
    color: '#E0E7FF',
    fontSize: 15,
    marginBottom: 16,
    borderWidth: 1,
    borderColor: 'rgba(99, 102, 241, 0.2)',
  },
  inputLabel: {
    color: '#6366f1',
    fontSize: 12,
    fontWeight: '800',
    marginBottom: 10,
    textTransform: 'uppercase',
    letterSpacing: 1,
  },
  buttonRow: { flexDirection: 'row', gap: 14 },
  button: {
    flex: 1,
    backgroundColor: '#4F46E5',
    paddingVertical: 16,
    borderRadius: 16,
    alignItems: 'center',
    shadowColor: '#4F46E5',
    shadowOffset: { width: 0, height: 4 },
    shadowOpacity: 0.4,
    shadowRadius: 8,
    elevation: 4,
  },
  buttonText: {
    color: '#ffffff',
    fontSize: 14,
    fontWeight: '900',
    letterSpacing: 1,
  },
  resultCard: {
    backgroundColor: 'rgba(139, 92, 246, 0.05)',
    padding: 20,
    borderRadius: 24,
    marginBottom: 24,
    borderWidth: 1,
    borderColor: 'rgba(139, 92, 246, 0.2)',
  },
  resultText: {
    color: '#A78BFA',
    fontSize: 14,
    lineHeight: 20,
    marginTop: 12,
    fontFamily: Platform.OS === 'ios' ? 'Menlo' : 'monospace',
  },
  advancedToggle: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  toggleArrow: { color: '#6366f1', fontSize: 14 },
  advancedContent: { marginTop: 20 },
  advancedButton: {
    backgroundColor: 'rgba(99, 102, 241, 0.05)',
    padding: 16,
    borderRadius: 16,
    marginTop: 12,
    borderWidth: 1,
    borderColor: 'rgba(99, 102, 241, 0.3)',
    alignItems: 'center',
  },
  advancedButtonText: {
    color: '#818CF8',
    fontSize: 14,
    fontWeight: '800',
  },
  rowBetween: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 16,
  },
  refreshText: {
    color: '#818CF8',
    fontSize: 13,
    fontWeight: '800',
    textTransform: 'uppercase',
  },
  keyList: { marginTop: 8 },
  keyItem: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: 16,
    borderBottomWidth: 1,
    borderBottomColor: 'rgba(255,255,255,0.05)',
  },
  keyActions: {
    flexDirection: 'row',
    gap: 16,
  },
  actionIcon: {
    fontSize: 18,
    opacity: 0.9,
  },
  keyText: {
    color: '#C4B5FD',
    fontSize: 15,
    fontWeight: '600',
  },
  emptyText: {
    color: '#475569',
    fontSize: 14,
    fontStyle: 'italic',
    textAlign: 'center',
    marginVertical: 20,
  },
  moreKeysText: {
    color: '#6366f1',
    fontSize: 12,
    textAlign: 'center',
    marginTop: 16,
    fontWeight: '800',
  },
  benchmarkCard: {
    backgroundColor: 'rgba(15, 15, 40, 0.6)',
    borderRadius: 32,
    padding: 24,
    borderWidth: 1,
    borderColor: 'rgba(14, 165, 233, 0.2)',
    shadowColor: '#0EA5E9',
    shadowOffset: { width: 0, height: 0 },
    shadowOpacity: 0.05,
    shadowRadius: 20,
  },
  benchmarkHeader: { marginBottom: 30, alignItems: 'center' },
  benchmarkSubtitle: {
    color: '#38BDF8',
    fontSize: 12,
    marginTop: 8,
    fontWeight: '700',
    letterSpacing: 2,
    textTransform: 'uppercase',
  },
  benchmarkButton: {
    backgroundColor: '#0EA5E9',
    paddingVertical: 18,
    borderRadius: 20,
    alignItems: 'center',
    justifyContent: 'center',
    shadowColor: '#0EA5E9',
    shadowOffset: { width: 0, height: 8 },
    shadowOpacity: 0.4,
    shadowRadius: 15,
    elevation: 8,
    marginBottom: 35,
  },
  benchmarkButtonDisabled: { opacity: 0.5 },
  benchmarkButtonText: {
    color: '#ffffff',
    fontSize: 16,
    fontWeight: '900',
    letterSpacing: 1,
  },
  resultTable: {
    backgroundColor: '#050511',
    borderRadius: 24,
    padding: 20,
    borderWidth: 1,
    borderColor: 'rgba(14, 165, 233, 0.1)',
  },
  resultRowContainer: {
    marginBottom: 20,
  },
  resultLabelRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 10,
  },
  resultLabel: { color: '#7DD3FC', fontSize: 13, fontWeight: '700' },
  resultValue: {
    fontSize: 14,
    fontWeight: '900',
    textAlign: 'right',
  },
  barBackground: {
    height: 8,
    backgroundColor: '#082f49',
    borderRadius: 4,
    overflow: 'hidden',
  },
  barForeground: {
    height: '100%',
    backgroundColor: '#38BDF8',
    borderRadius: 4,
    shadowColor: '#38BDF8',
    shadowOffset: { width: 0, height: 0 },
    shadowOpacity: 0.8,
    shadowRadius: 4,
  },
  secureDBColor: { color: '#7DD3FC' },
  winnerBanner: {
    backgroundColor: 'rgba(16, 185, 129, 0.05)',
    padding: 18,
    borderRadius: 20,
    marginTop: 30,
    borderWidth: 1,
    borderColor: 'rgba(16, 185, 129, 0.3)',
    shadowColor: '#10B981',
    shadowOffset: { width: 0, height: 0 },
    shadowOpacity: 0.2,
    shadowRadius: 10,
  },
  winnerText: {
    color: '#34D399',
    fontSize: 14,
    fontWeight: '900',
    textAlign: 'center',
    letterSpacing: 0.5,
  },
  statusBanner: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: 16,
    borderRadius: 18,
    marginBottom: 24,
    borderWidth: 1,
  },
  statusSuccess: {
    backgroundColor: 'rgba(16, 185, 129, 0.05)',
    borderColor: 'rgba(16, 185, 129, 0.4)',
  },
  statusError: {
    backgroundColor: 'rgba(239, 68, 68, 0.05)',
    borderColor: 'rgba(239, 68, 68, 0.4)',
  },
  statusInfo: {
    backgroundColor: 'rgba(99, 102, 241, 0.05)',
    borderColor: 'rgba(99, 102, 241, 0.4)',
  },
  statusText: {
    flex: 1,
    fontSize: 15,
    fontWeight: '800',
    color: '#FFFFFF',
  },
  statusTextError: { color: '#FCA5A5' },
  statusClose: {
    fontSize: 24,
    color: '#94A3B8',
    marginLeft: 16,
    fontWeight: '400',
  },
});

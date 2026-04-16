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
import { SecureDB } from 'react-native-secure-db';

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

      const docsDir = SecureDB.getDocumentsDirectory();
      const benchPath = `${docsDir}/bench_turbo_standalone.db`;
      const secureDB = new SecureDB(benchPath, 20 * 1024 * 1024);

      // Force initial installation
      SecureDB.install();
      secureDB.clear();

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
      secureDB.setMulti(entries);
      times.push(Date.now() - insertStart);

      const readStart = Date.now();
      for (let i = 0; i < NUM_QUERIES; i++) {
        secureDB.get(`key_${Math.floor(Math.random() * NUM_OPERATIONS)}`);
      }
      times.push(Date.now() - readStart);

      const rangeStart = Date.now();
      secureDB.rangeQuery('key_100', 'key_300');
      times.push(Date.now() - rangeStart);

      const deleteStart = Date.now();
      secureDB.clear();
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
  const [activeTab, setActiveTab] = useState<'demo' | 'benchmark'>('demo');
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

  const db = useMemo(() => {
    const docPath = SecureDB.getDocumentsDirectory();
    const dbFile = `${docPath}/secure_v1.db`;
    return new SecureDB(dbFile, 10 * 1024 * 1024);
  }, []);

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
    SecureDB.install();
    const docPath = SecureDB.getDocumentsDirectory();
    setDbPath(docPath);
    const dbFile = `${docPath}/secure_v1.db`;
    setDb(new SecureDB(dbFile, 10 * 1024 * 1024));
  }, []);

  useEffect(() => {
    if (db) {
      refreshKeys();
    }
  }, [db, refreshKeys]);

  const handleSet = () => {
    if (!key) return Alert.alert('Error', 'Please enter a key');
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

  const handleDel = () => {
    if (!key) {
      setStatusType('error');
      setStatusMessage('Please enter a key');
      return;
    }
    if (!db) {
      setStatusType('error');
      setStatusMessage('Database not initialized');
      return;
    }
    Alert.alert('Delete Key', `Are you sure you want to delete "${key}"?`, [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Delete',
        style: 'destructive',
        onPress: () => {
          try {
            const result = db.del(key);
            if (result) {
              setGetResult('');
              refreshKeys();
              setStatusType('success');
              setStatusMessage(`🗑️ Deleted: ${key}`);
              setTimeout(() => setStatusMessage(''), 2000);
            } else {
              setStatusType('error');
              setStatusMessage(`Key not found: ${key}`);
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
    if (!rangeStart || !rangeEnd) {
      Alert.alert('Error', 'Please enter start and end keys');
      return;
    }
    const results = db?.rangeQuery(rangeStart, rangeEnd);
    Alert.alert('Range Query', `Found ${results?.length} items`, [
      { text: 'OK' },
      {
        text: 'View Details',
        onPress: () => console.log('Range Results:', results),
      },
    ]);
  };

  const handleTurboInsert = () => {
    const start = Date.now();
    const batch: Record<string, any> = {};
    for (let i = 0; i < 500; i++) {
      batch[`turbo_${i}`] = { id: i, ts: Date.now() };
    }
    db?.setMulti(batch);
    const elapsed = Date.now() - start;
    refreshKeys();
    Alert.alert('Turbo Success', `Inserted 500 records in ${elapsed}ms`);
  };

  return (
    <SafeAreaView style={styles.container}>
      <ScrollView contentContainerStyle={styles.scrollContent}>
        <View style={styles.header}>
          <Text style={styles.title}>SecureDB</Text>
          <Text style={styles.subtitle}>Unified Crypto + JSI B-Tree</Text>
        </View>

        <View style={styles.tabContainer}>
          {['demo', 'benchmark'].map((tab) => (
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
                  onPress={handleDel}
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
                    onPress={() => {
                      db?.clear();
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
                          setKey(k);
                          handleGet();
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
                            setKey(k);
                            handleGet();
                          }}
                        >
                          <Text style={styles.actionIcon}>🔍</Text>
                        </TouchableOpacity>
                        <TouchableOpacity
                          onPress={() => {
                            setKey(k);
                            handleDel();
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
        ) : (
          <BenchmarkPage />
        )}
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#020617' },
  scrollContent: { padding: 20, paddingBottom: 40 },
  header: { alignItems: 'center', marginVertical: 35 },
  title: {
    fontSize: 44,
    fontWeight: '900',
    color: '#F8FAFC',
    letterSpacing: -2,
    textShadowColor: 'rgba(56, 189, 248, 0.3)',
    textShadowOffset: { width: 0, height: 4 },
    textShadowRadius: 10,
  },
  subtitle: {
    fontSize: 10,
    color: '#38BDF8',
    fontWeight: '800',
    textTransform: 'uppercase',
    letterSpacing: 3,
    marginTop: 4,
  },
  tabContainer: {
    flexDirection: 'row',
    backgroundColor: '#0F172A',
    borderRadius: 20,
    padding: 6,
    marginBottom: 25,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.03)',
  },
  tab: { flex: 1, paddingVertical: 12, alignItems: 'center', borderRadius: 14 },
  activeTab: {
    backgroundColor: '#1E293B',
    borderWidth: 1,
    borderColor: 'rgba(56, 189, 248, 0.2)',
  },
  tabText: { color: '#64748B', fontSize: 11, fontWeight: '800' },
  activeTabText: { color: '#F8FAFC' },
  metaCard: {
    backgroundColor: '#0F172A',
    borderRadius: 20,
    padding: 16,
    marginBottom: 20,
    borderLeftWidth: 4,
    borderLeftColor: '#38BDF8',
  },
  metaLabel: { color: '#475569', fontSize: 10, fontWeight: '900' },
  metaValue: {
    color: '#94A3B8',
    fontSize: 11,
    marginTop: 6,
    fontFamily: Platform.OS === 'ios' ? 'Menlo' : 'monospace',
  },
  card: {
    backgroundColor: '#0F172A',
    borderRadius: 24,
    padding: 20,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.04)',
    marginBottom: 20,
  },
  cardTitle: {
    color: '#F8FAFC',
    fontSize: 16,
    fontWeight: '800',
    marginBottom: 18,
  },
  input: {
    backgroundColor: '#020617',
    borderRadius: 14,
    padding: 16,
    color: '#F8FAFC',
    fontSize: 15,
    marginBottom: 15,
    borderWidth: 1,
    borderColor: '#1E293B',
  },
  inputLabel: {
    color: '#64748B',
    fontSize: 11,
    fontWeight: '700',
    marginBottom: 10,
    textTransform: 'uppercase',
  },
  buttonRow: { flexDirection: 'row', gap: 12 },
  button: {
    flex: 1,
    backgroundColor: '#F8FAFC',
    paddingVertical: 15,
    borderRadius: 14,
    alignItems: 'center',
    shadowColor: '#fff',
    shadowOffset: { width: 0, height: 2 },
    shadowOpacity: 0.1,
    shadowRadius: 4,
    elevation: 3,
  },
  buttonText: { color: '#020617', fontSize: 13, fontWeight: '900' },
  resultCard: {
    backgroundColor: 'rgba(56, 189, 248, 0.05)',
    padding: 18,
    borderRadius: 20,
    marginBottom: 20,
    borderWidth: 1,
    borderColor: 'rgba(56, 189, 248, 0.15)',
  },
  resultText: {
    color: '#38BDF8',
    fontSize: 13,
    lineHeight: 18,
    marginTop: 10,
    fontFamily: Platform.OS === 'ios' ? 'Menlo' : 'monospace',
  },
  advancedToggle: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  toggleArrow: { color: '#475569', fontSize: 12 },
  advancedContent: { marginTop: 20 },
  advancedButton: {
    backgroundColor: '#020617',
    padding: 16,
    borderRadius: 14,
    marginTop: 12,
    borderWidth: 1,
    borderColor: 'rgba(56, 189, 248, 0.3)',
    alignItems: 'center',
  },
  advancedButtonText: {
    color: '#38BDF8',
    fontSize: 13,
    fontWeight: '800',
  },
  rowBetween: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 15,
  },
  refreshText: { color: '#38BDF8', fontSize: 13, fontWeight: '800' },
  keyList: { marginTop: 5 },
  keyItem: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingVertical: 14,
    borderBottomWidth: 1,
    borderBottomColor: 'rgba(255,255,255,0.03)',
  },
  keyActions: {
    flexDirection: 'row',
    gap: 15,
  },
  actionIcon: {
    fontSize: 16,
    opacity: 0.8,
  },
  keyText: {
    color: '#94A3B8',
    fontSize: 14,
    fontWeight: '500',
  },
  emptyText: {
    color: '#475569',
    fontSize: 13,
    fontStyle: 'italic',
    textAlign: 'center',
    marginVertical: 15,
  },
  moreKeysText: {
    color: '#475569',
    fontSize: 11,
    textAlign: 'center',
    marginTop: 15,
    fontWeight: '700',
  },
  benchmarkCard: {
    backgroundColor: '#0F172A',
    borderRadius: 28,
    padding: 24,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.04)',
  },
  benchmarkHeader: { marginBottom: 25 },
  benchmarkSubtitle: {
    color: '#64748B',
    fontSize: 12,
    marginTop: 6,
    fontWeight: '600',
  },
  benchmarkButton: {
    backgroundColor: '#38BDF8',
    paddingVertical: 16,
    borderRadius: 16,
    alignItems: 'center',
    justifyContent: 'center',
    shadowColor: '#38BDF8',
    shadowOffset: { width: 0, height: 4 },
    shadowOpacity: 0.3,
    shadowRadius: 8,
    elevation: 6,
    marginBottom: 30,
  },
  benchmarkButtonDisabled: { opacity: 0.5 },
  benchmarkButtonText: { color: '#fff', fontSize: 14, fontWeight: '900' },
  resultTable: {
    backgroundColor: '#020617',
    borderRadius: 20,
    padding: 16,
  },
  resultRowContainer: {
    marginBottom: 18,
  },
  resultLabelRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 8,
  },
  resultLabel: { color: '#94A3B8', fontSize: 12, fontWeight: '700' },
  resultValue: {
    fontSize: 13,
    fontWeight: '900',
    textAlign: 'right',
  },
  barBackground: {
    height: 6,
    backgroundColor: '#1E293B',
    borderRadius: 3,
    overflow: 'hidden',
  },
  barForeground: {
    height: '100%',
    backgroundColor: '#38BDF8',
    borderRadius: 3,
  },
  secureDBColor: { color: '#38BDF8' },
  winnerBanner: {
    backgroundColor: 'rgba(16, 185, 129, 0.1)',
    padding: 16,
    borderRadius: 16,
    marginTop: 25,
    borderWidth: 1,
    borderColor: 'rgba(16, 185, 129, 0.2)',
  },
  winnerText: {
    color: '#10B981',
    fontSize: 13,
    fontWeight: '900',
    textAlign: 'center',
  },
  statusBanner: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: 14,
    borderRadius: 16,
    marginBottom: 20,
    borderWidth: 1,
  },
  statusSuccess: {
    backgroundColor: 'rgba(16, 185, 129, 0.1)',
    borderColor: 'rgba(16, 185, 129, 0.3)',
  },
  statusError: {
    backgroundColor: 'rgba(239, 68, 68, 0.1)',
    borderColor: 'rgba(239, 68, 68, 0.3)',
  },
  statusInfo: {
    backgroundColor: 'rgba(56, 189, 248, 0.1)',
    borderColor: 'rgba(56, 189, 248, 0.3)',
  },
  statusText: {
    flex: 1,
    fontSize: 14,
    fontWeight: '700',
    color: '#F8FAFC',
  },
  statusTextError: { color: '#EF4444' },
  statusClose: {
    fontSize: 22,
    color: '#64748B',
    marginLeft: 12,
    fontWeight: '300',
  },
});

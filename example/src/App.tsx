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
} from 'react-native';
import { SecureDB } from 'react-native-secure-db';

const BenchmarkPage = () => {
  const [results, setResults] = useState<number[]>([]);
  const [isRunning, setIsRunning] = useState(false);

  const NUM_OPERATIONS = 1000;
  const NUM_QUERIES = 2000;

  const runBenchmark = async () => {
    try {
      setIsRunning(true);
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

  const Row = ({ label, idx }: { label: string; idx: number }) => (
    <View style={styles.resultRow}>
      <Text style={styles.resultLabel}>{label}</Text>
      <Text style={[styles.resultValue, styles.secureDBColor]}>
        {formatTime(results[idx])}
      </Text>
    </View>
  );

  return (
    <View style={styles.benchmarkCard}>
      <View style={styles.benchmarkHeader}>
        <Text style={styles.cardTitle}>Performance Lab</Text>
        <Text style={styles.benchmarkSubtitle}>
          {NUM_OPERATIONS} records | {NUM_QUERIES} reads
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
          <Text style={styles.benchmarkButtonText}>Run Native Benchmark</Text>
        )}
      </TouchableOpacity>

      <View style={styles.resultTable}>
        <View style={styles.resultHeader}>
          <Text style={[styles.resultLabel, { color: '#94A3B8' }]}>
            Operation
          </Text>
          <Text style={styles.resultHeaderCell}>Duration</Text>
        </View>
        <Row label="Bulk Insert" idx={0} />
        <Row label="Random Read" idx={1} />
        <Row label="Range Query" idx={2} />
        <Row label="Bulk Delete" idx={3} />
      </View>

      {results.length > 0 && (
        <View style={styles.winnerBanner}>
          <Text style={styles.winnerText}>
            ⚡ Turbo Mode Active: Average{' '}
            {(results.reduce((a, b) => a + b, 0) / 4).toFixed(1)}ms per sequence
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
  const [rangeStart, setRangeStart] = useState('a');
  const [rangeEnd, setRangeEnd] = useState('z');

  const db = useMemo(() => {
    const docPath = SecureDB.getDocumentsDirectory();
    const dbFile = `${docPath}/secure_v1.db`;
    return new SecureDB(dbFile, 10 * 1024 * 1024);
  }, []);

  const refreshKeys = useCallback(() => {
    try {
      setAllKeys(db.getAllKeys());
    } catch (e) {
      console.error('Refresh Keys Error:', e);
    }
  }, [db]);

  useEffect(() => {
    SecureDB.install();
    setDbPath(SecureDB.getDocumentsDirectory());
    refreshKeys();
  }, [refreshKeys]);

  const handleSet = () => {
    if (!key) return Alert.alert('Error', 'Key required');
    try {
      const data =
        value.startsWith('{') || value.startsWith('[')
          ? JSON.parse(value)
          : value;
      if (db.set(key, data)) {
        setKey('');
        setValue('');
        refreshKeys();
      }
    } catch (e) {
      Alert.alert('Error', String(e));
    }
  };

  const handleGet = () => {
    const res = db.get(key);
    setGetResult(
      res === undefined ? 'Not found' : JSON.stringify(res, null, 2)
    );
  };

  const handleRange = () => {
    const results = db.rangeQuery(rangeStart, rangeEnd);
    Alert.alert(
      'Range Query',
      `Found ${results.length} items. Check console for details.`
    );
    console.log('Range Results:', results);
  };

  const handleTurboInsert = () => {
    const start = Date.now();
    const batch: Record<string, any> = {};
    for (let i = 0; i < 500; i++)
      batch[`turbo_${i}`] = { id: i, ts: Date.now() };
    db.setMulti(batch);
    Alert.alert(
      'Turbo Success',
      `Inserted 500 records in ${Date.now() - start}ms`
    );
    refreshKeys();
  };

  return (
    <SafeAreaView style={styles.container}>
      <ScrollView contentContainerStyle={styles.scrollContent}>
        <View style={styles.header}>
          <Text style={styles.title}>SecureDB</Text>
          <Text style={styles.subtitle}>Native Turbo Engine</Text>
        </View>

        <View style={styles.tabContainer}>
          {['demo', 'benchmark'].map((tab) => (
            <TouchableOpacity
              key={tab}
              style={[styles.tab, activeTab === tab && styles.activeTab]}
              onPress={() => setActiveTab(tab as any)}
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
                PATH: {Platform.OS.toUpperCase()}
              </Text>
              <Text style={styles.metaValue} numberOfLines={1}>
                {dbPath}
              </Text>
            </View>

            <View style={styles.card}>
              <Text style={styles.cardTitle}>Instant Access</Text>
              <TextInput
                style={styles.input}
                value={key}
                onChangeText={setKey}
                placeholder="Key..."
                placeholderTextColor="#475569"
              />
              <TextInput
                style={[styles.input, { height: 60 }]}
                value={value}
                onChangeText={setValue}
                placeholder="Value (String or JSON)..."
                placeholderTextColor="#475569"
                multiline
              />

              <View style={styles.buttonRow}>
                <TouchableOpacity style={styles.button} onPress={handleSet}>
                  <Text style={styles.buttonText}>SET</Text>
                </TouchableOpacity>
                <TouchableOpacity
                  style={[styles.button, { backgroundColor: '#38BDF8' }]}
                  onPress={handleGet}
                >
                  <Text style={[styles.buttonText, { color: '#fff' }]}>
                    GET
                  </Text>
                </TouchableOpacity>
                <TouchableOpacity
                  style={[styles.button, { backgroundColor: '#EF4444' }]}
                  onPress={() => {
                    db.del(key);
                    refreshKeys();
                  }}
                >
                  <Text style={[styles.buttonText, { color: '#fff' }]}>
                    DEL
                  </Text>
                </TouchableOpacity>
              </View>
            </View>

            {getResult !== '' && (
              <View style={styles.resultCard}>
                <Text style={styles.resultText}>{getResult}</Text>
              </View>
            )}

            <View style={styles.card}>
              <TouchableOpacity
                style={styles.advancedToggle}
                onPress={() => setShowAdvanced(!showAdvanced)}
              >
                <Text style={styles.cardTitle}>Turbo Extensions</Text>
                <Text style={styles.toggleArrow}>
                  {showAdvanced ? '▼' : '▲'}
                </Text>
              </TouchableOpacity>

              {showAdvanced && (
                <View style={styles.advancedContent}>
                  <Text style={styles.inputLabel}>Range Explorer</Text>
                  <View style={styles.buttonRow}>
                    <TextInput
                      style={[styles.input, { flex: 1, marginBottom: 0 }]}
                      value={rangeStart}
                      onChangeText={setRangeStart}
                      placeholder="Start"
                    />
                    <TextInput
                      style={[styles.input, { flex: 1, marginBottom: 0 }]}
                      value={rangeEnd}
                      onChangeText={setRangeEnd}
                      placeholder="End"
                    />
                  </View>
                  <TouchableOpacity
                    style={styles.advancedButton}
                    onPress={handleRange}
                  >
                    <Text style={styles.advancedButtonText}>
                      Execute Native Range Query
                    </Text>
                  </TouchableOpacity>
                  <TouchableOpacity
                    style={[styles.advancedButton, { borderColor: '#22C55E' }]}
                    onPress={handleTurboInsert}
                  >
                    <Text
                      style={[styles.advancedButtonText, { color: '#22C55E' }]}
                    >
                      Bulk Turbo Insert (500 ops)
                    </Text>
                  </TouchableOpacity>
                  <TouchableOpacity
                    style={[styles.advancedButton, { borderColor: '#EF4444' }]}
                    onPress={() => {
                      db.clear();
                      refreshKeys();
                    }}
                  >
                    <Text
                      style={[styles.advancedButtonText, { color: '#EF4444' }]}
                    >
                      Wipe Database
                    </Text>
                  </TouchableOpacity>
                </View>
              )}
            </View>

            <View style={styles.card}>
              <View style={styles.rowBetween}>
                <Text style={styles.cardTitle}>
                  Index Map ({allKeys.length})
                </Text>
                <TouchableOpacity onPress={refreshKeys}>
                  <Text style={styles.refreshText}>↻ Sync</Text>
                </TouchableOpacity>
              </View>
              <View style={styles.keyList}>
                {allKeys.length === 0 ? (
                  <Text style={styles.emptyText}>Index empty.</Text>
                ) : (
                  allKeys.slice(0, 15).map((k) => (
                    <TouchableOpacity
                      key={k}
                      onPress={() => setKey(k)}
                      style={styles.keyItem}
                    >
                      <Text style={styles.keyText}>
                        0x
                        {Math.abs(
                          k.split('').reduce((a, b) => {
                            a = (a << 5) - a + b.charCodeAt(0);
                            return a & a;
                          }, 0)
                        ).toString(16)}{' '}
                        → {k}
                      </Text>
                    </TouchableOpacity>
                  ))
                )}
                {allKeys.length > 15 && (
                  <Text style={styles.moreKeysText}>
                    + {allKeys.length - 15} items hidden
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
  scrollContent: { padding: 20 },
  header: { alignItems: 'center', marginVertical: 30 },
  title: {
    fontSize: 48,
    fontWeight: '900',
    color: '#F8FAFC',
    letterSpacing: -2,
  },
  subtitle: {
    fontSize: 12,
    color: '#38BDF8',
    fontWeight: 'bold',
    textTransform: 'uppercase',
    letterSpacing: 2,
  },
  tabContainer: {
    flexDirection: 'row',
    backgroundColor: '#0F172A',
    borderRadius: 16,
    padding: 6,
    marginBottom: 25,
  },
  tab: { flex: 1, paddingVertical: 12, alignItems: 'center', borderRadius: 12 },
  activeTab: {
    backgroundColor: '#1E293B',
    borderWidth: 1,
    borderColor: '#334155',
  },
  tabText: { color: '#64748B', fontSize: 11, fontWeight: '800' },
  activeTabText: { color: '#F8FAFC' },
  metaCard: {
    backgroundColor: '#0F172A',
    borderRadius: 16,
    padding: 15,
    marginBottom: 20,
    borderLeftWidth: 3,
    borderLeftColor: '#38BDF8',
  },
  metaLabel: { color: '#475569', fontSize: 10, fontWeight: '900' },
  metaValue: {
    color: '#94A3B8',
    fontSize: 12,
    marginTop: 4,
    fontFamily: Platform.OS === 'ios' ? 'Menlo' : 'monospace',
  },
  card: {
    backgroundColor: '#0F172A',
    borderRadius: 24,
    padding: 20,
    borderWidth: 1,
    borderColor: '#1E293B',
    marginBottom: 20,
  },
  cardTitle: {
    color: '#F8FAFC',
    fontSize: 15,
    fontWeight: '800',
    marginBottom: 15,
  },
  input: {
    backgroundColor: '#020617',
    borderRadius: 12,
    padding: 14,
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
    marginBottom: 8,
    textTransform: 'uppercase',
  },
  buttonRow: { flexDirection: 'row', gap: 10 },
  button: {
    flex: 1,
    backgroundColor: '#F8FAFC',
    paddingVertical: 14,
    borderRadius: 12,
    alignItems: 'center',
  },
  buttonText: { color: '#020617', fontSize: 13, fontWeight: '900' },
  resultCard: {
    backgroundColor: 'rgba(56, 189, 248, 0.05)',
    padding: 15,
    borderRadius: 16,
    marginBottom: 20,
    borderWidth: 1,
    borderColor: 'rgba(56, 189, 248, 0.1)',
  },
  resultText: {
    color: '#38BDF8',
    fontSize: 13,
    fontFamily: Platform.OS === 'ios' ? 'Menlo' : 'monospace',
  },
  advancedToggle: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  toggleArrow: { color: '#475569', fontSize: 10 },
  advancedContent: { marginTop: 20 },
  advancedButton: {
    backgroundColor: '#020617',
    padding: 16,
    borderRadius: 14,
    marginTop: 10,
    borderWidth: 1,
    borderColor: '#38BDF8',
  },
  advancedButtonText: {
    color: '#38BDF8',
    fontSize: 12,
    fontWeight: '800',
    textAlign: 'center',
  },
  rowBetween: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 15,
  },
  refreshText: { color: '#38BDF8', fontSize: 12, fontWeight: '700' },
  keyList: { marginTop: 5 },
  keyItem: {
    paddingVertical: 10,
    borderBottomWidth: 1,
    borderBottomColor: '#1E293B',
  },
  keyText: {
    color: '#94A3B8',
    fontSize: 12,
    fontFamily: Platform.OS === 'ios' ? 'Menlo' : 'monospace',
  },
  emptyText: {
    color: '#475569',
    fontSize: 12,
    fontStyle: 'italic',
    textAlign: 'center',
    marginVertical: 10,
  },
  moreKeysText: {
    color: '#475569',
    fontSize: 10,
    textAlign: 'center',
    marginTop: 10,
    fontWeight: '700',
  },
  benchmarkCard: { backgroundColor: '#0F172A', borderRadius: 24, padding: 20 },
  benchmarkHeader: { marginBottom: 20 },
  benchmarkSubtitle: {
    color: '#64748B',
    fontSize: 11,
    marginTop: 4,
    fontWeight: '600',
  },
  benchmarkButtonRow: { flexDirection: 'row', gap: 10, marginBottom: 20 },
  benchmarkButton: {
    backgroundColor: '#38BDF8',
    paddingVertical: 15,
    borderRadius: 14,
    alignItems: 'center',
    justifyContent: 'center',
  },
  benchmarkButtonDisabled: { opacity: 0.5 },
  benchmarkButtonText: { color: '#fff', fontSize: 13, fontWeight: '900' },
  resultTable: {
    borderRadius: 16,
    overflow: 'hidden',
    backgroundColor: '#020617',
    padding: 10,
  },
  resultHeader: {
    flexDirection: 'row',
    paddingVertical: 10,
    borderBottomWidth: 1,
    borderBottomColor: '#1E293B',
  },
  resultHeaderCell: {
    flex: 1,
    color: '#F8FAFC',
    fontSize: 10,
    fontWeight: '900',
    textAlign: 'center',
  },
  resultRow: {
    flexDirection: 'row',
    paddingVertical: 12,
    borderBottomWidth: 1,
    borderBottomColor: '#0F172A',
  },
  resultLabel: { flex: 1, color: '#64748B', fontSize: 11, fontWeight: '700' },
  resultValue: {
    flex: 1,
    fontSize: 11,
    fontWeight: '800',
    textAlign: 'center',
  },
  secureDBColor: { color: '#38BDF8' },
  winnerBanner: {
    backgroundColor: 'rgba(34, 197, 94, 0.1)',
    padding: 12,
    borderRadius: 12,
    marginTop: 20,
  },
  winnerText: {
    color: '#22C55E',
    fontSize: 12,
    fontWeight: '900',
    textAlign: 'center',
  },
});

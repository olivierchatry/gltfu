# Progress Reporting

All `gltfu` commands now support streaming progress updates in both human-readable and JSON formats.

## Usage

### Human-Readable Output (Default)

```bash
./gltfu dedupe input.gltf -o output.gltf
```

Output:
```
[dedupe] Loading file - input.gltf
[dedupe] File loaded, starting deduplication
[dedupe-accessors] Deduplicating accessors - 100000 total
[dedupe-accessors] Hashed 10000/100000 accessors (14%)
...
✓ Successfully deduplicated to: output.gltf
```

### JSON Streaming Output

For automation and UI integration:

```bash
./gltfu --json-progress dedupe input.gltf -o output.gltf
```

Output (one JSON object per line):
```json
{"type":"progress","operation":"dedupe","message":"Loading file","progress":0.0,"details":"input.gltf"}
{"type":"progress","operation":"dedupe","message":"File loaded, starting deduplication","progress":0.2}
{"type":"progress","operation":"dedupe-accessors","message":"Deduplicating accessors","progress":0.0,"details":"100000 total"}
{"type":"progress","operation":"dedupe-accessors","message":"Computing content hashes","progress":0.1}
{"type":"progress","operation":"dedupe-accessors","message":"Hashed 10000/100000 accessors","progress":0.14}
{"type":"progress","operation":"dedupe-accessors","message":"Grouped into 1234 metadata buckets","progress":0.4}
{"type":"progress","operation":"dedupe-accessors","message":"Finding duplicates using content hashes","progress":0.5}
{"type":"progress","operation":"dedupe-accessors","message":"Found 567 duplicates","progress":0.8}
{"type":"progress","operation":"dedupe","message":"Deduplication complete","progress":0.9}
{"type":"progress","operation":"dedupe","message":"Saving output","progress":0.95,"details":"output.gltf"}
{"type":"success","operation":"dedupe","message":"Successfully deduplicated to: output.gltf"}
```

## JSON Format

### Progress Event
```json
{
  "type": "progress",
  "operation": "string",     // Operation identifier (e.g., "dedupe", "merge", "flatten")
  "message": "string",        // Human-readable progress message
  "progress": 0.0-1.0,       // Optional: progress percentage (0.0-1.0), -1 if indeterminate
  "details": "string"         // Optional: additional details
}
```

### Success Event
```json
{
  "type": "success",
  "operation": "string",      // Operation identifier
  "message": "string"         // Success message
}
```

### Error Event
```json
{
  "type": "error",
  "operation": "string",      // Operation identifier
  "message": "string"         // Error message
}
```

## Operations

Each command reports progress with specific operation identifiers:

### merge
- `merge` - Overall merge progress
- Stages: loading files, merging models, saving output

### dedupe
- `dedupe` - Overall deduplication progress
- `dedupe-accessors` - Accessor deduplication (often the longest)
- `dedupe-meshes` - Mesh deduplication
- `dedupe-materials` - Material deduplication
- `dedupe-textures` - Texture/image deduplication

### flatten
- `flatten` - Scene graph flattening
- Stages: loading, flattening, writing

### join
- `join` - Primitive joining
- Stages: loading, joining, writing

### weld
- `weld` - Vertex welding
- Stages: loading, welding, writing

### prune
- `prune` - Unused resource removal
- Stages: loading, pruning, writing

### simplify
- `simplify` - Mesh simplification
- Stages: loading, simplifying, writing

## Integration Example

### Python
```python
import subprocess
import json

process = subprocess.Popen(
    ['./gltfu', '--json-progress', 'dedupe', 'input.gltf', '-o', 'output.gltf'],
    stdout=subprocess.PIPE,
    text=True
)

for line in process.stdout:
    event = json.loads(line)
    
    if event['type'] == 'progress':
        operation = event['operation']
        message = event['message']
        progress = event.get('progress', -1)
        
        if progress >= 0:
            print(f"{operation}: {message} ({int(progress*100)}%)")
        else:
            print(f"{operation}: {message}")
    
    elif event['type'] == 'error':
        print(f"ERROR: {event['message']}")
        break
    
    elif event['type'] == 'success':
        print(f"SUCCESS: {event['message']}")

process.wait()
```

### JavaScript/Node.js
```javascript
const { spawn } = require('child_process');

const gltfu = spawn('./gltfu', ['--json-progress', 'dedupe', 'input.gltf', '-o', 'output.gltf']);

gltfu.stdout.on('data', (data) => {
    const lines = data.toString().split('\n').filter(l => l.trim());
    
    for (const line of lines) {
        try {
            const event = JSON.parse(line);
            
            if (event.type === 'progress') {
                const progress = event.progress >= 0 ? `(${Math.round(event.progress * 100)}%)` : '';
                console.log(`${event.operation}: ${event.message} ${progress}`);
            } else if (event.type === 'error') {
                console.error(`ERROR: ${event.message}`);
            } else if (event.type === 'success') {
                console.log(`SUCCESS: ${event.message}`);
            }
        } catch (e) {
            // Ignore parse errors for partial lines
        }
    }
});

gltfu.on('close', (code) => {
    console.log(`Process exited with code ${code}`);
});
```

## Performance Notes

- Progress reporting adds minimal overhead (< 1% in most cases)
- JSON output suppresses tinygltf warnings to keep output clean
- Progress values are estimates based on the current stage
- For large deduplication operations (100K+ accessors), progress updates every 10K items

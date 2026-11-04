'use strict';

const fs = require('node:fs');
const path = require('node:path');
const {spawn} = require('node:child_process');
const vscode = require('vscode');

const LANGUAGE = 'tacqor';
const MAX_SOURCE = 1_000_000;

class Scanner {
    constructor(root, diagnostics, output) {
        this.root = root;
        this.diagnostics = diagnostics;
        this.output = output;
        this.pending = new Map();
        this.warned = false;
    }

    executable() {
        const configured = vscode.workspace.getConfiguration('tacqor').get('compilerPath').trim();
        if (configured) return configured;
        if (process.platform === 'linux' && process.arch === 'x64') {
            const bundled = path.join(this.root, 'compiler', 'tacqor');
            try {
                fs.accessSync(bundled, fs.constants.X_OK);
            } catch {
                fs.chmodSync(bundled, 0o755);
            }
            return bundled;
        }
        return process.platform === 'win32' ? 'tacqor.exe' : 'tacqor';
    }

    issue(document, item) {
        const line = Math.max(0, Math.min(item.line - 1, document.lineCount - 1));
        const bytes = Buffer.from(document.lineAt(line).text, 'utf8');
        const startByte = Math.max(0, item.column - 1);
        const offset = byte => bytes.subarray(0, byte).toString('utf8').length;
        const range = new vscode.Range(line, offset(startByte), line, offset(startByte + item.length));
        const diagnostic = new vscode.Diagnostic(
            range, item.message,
            item.severity === 1 ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning,
        );
        diagnostic.code = item.code;
        diagnostic.source = 'Tacqor';
        return diagnostic;
    }

    check(document) {
        if (document.languageId !== LANGUAGE) return;
        const key = document.uri.toString();
        this.cancel(key);
        const job = {version: document.version};
        this.pending.set(key, job);
        job.timer = setTimeout(() => this.scan(document, key, job), 180);
    }

    scan(document, key, job) {
        if (this.pending.get(key) !== job || document.isClosed) return;
        const text = document.getText();
        if (Buffer.byteLength(text) > MAX_SOURCE) {
            this.complete(document, key, job, [{
                line: 1, column: 1, length: 1, severity: 2, code: 'source-too-large',
                message: 'Live checking supports source files up to one million bytes.',
            }]);
            return;
        }
        if (text.split('\n').every(line => /^\s*(?:#.*)?$/.test(line))) {
            this.complete(document, key, job, []);
            return;
        }
        try {
            // Pass the document's path so that relative imports resolve from its folder.
            const args = ['diagnose', '--stdin'];
            if (document.uri.scheme === 'file') args.push(document.uri.fsPath);
            const child = spawn(this.executable(), args, {
                cwd: this.root, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'],
            });
            job.child = child;
            let out = '', err = '';
            child.stdout.setEncoding('utf8').on('data', data => { out += data; });
            child.stderr.setEncoding('utf8').on('data', data => { err += data; });
            child.stdin.on('error', () => {});
            child.on('error', error => this.fail(key, job, error.message));
            child.on('close', code => {
                if (this.pending.get(key) !== job) return;
                try {
                    if (code !== 0 && code !== 1) throw new Error(err || `compiler exited with status ${code}`);
                    this.complete(document, key, job, JSON.parse(out));
                } catch (error) {
                    this.fail(key, job, err || error.message);
                }
            });
            child.stdin.end(text);
        } catch (error) {
            this.fail(key, job, error.message);
        }
    }

    complete(document, key, job, items) {
        if (this.pending.get(key) !== job) return;
        if (!document.isClosed && document.version === job.version) {
            this.diagnostics.set(document.uri, items.map(item => this.issue(document, item)));
        }
        this.pending.delete(key);
    }

    fail(key, job, message) {
        if (this.pending.get(key) !== job) return;
        this.pending.delete(key);
        this.output.appendLine(message);
        if (!this.warned) {
            this.warned = true;
            vscode.window.showWarningMessage('Tacqor scanner could not start. Check Tacqor: Compiler Path.');
        }
    }

    cancel(key) {
        const job = this.pending.get(key);
        if (job) {
            this.pending.delete(key);
            clearTimeout(job.timer);
            if (job.child) job.child.kill();
        }
    }

    close(document) {
        this.cancel(document.uri.toString());
        this.diagnostics.delete(document.uri);
    }

    dispose() {
        for (const key of this.pending.keys()) this.cancel(key);
    }
}

function activate(context) {
    const diagnostics = vscode.languages.createDiagnosticCollection(LANGUAGE);
    const output = vscode.window.createOutputChannel('Tacqor');
    const scanner = new Scanner(context.extensionPath, diagnostics, output);
    const refresh = () => {
        scanner.dispose();
        scanner.warned = false;
        diagnostics.clear();
        for (const document of vscode.workspace.textDocuments) scanner.check(document);
    };
    context.subscriptions.push(
        scanner, diagnostics, output,
        vscode.workspace.onDidOpenTextDocument(document => scanner.check(document)),
        vscode.workspace.onDidChangeTextDocument(event => {
            if (event.contentChanges.length) scanner.check(event.document);
        }),
        vscode.workspace.onDidCloseTextDocument(document => scanner.close(document)),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (event.affectsConfiguration('tacqor.compilerPath')) refresh();
        }),
        vscode.commands.registerCommand('tacqor.restartScanner', refresh),
    );
    refresh();
}

module.exports = {activate};

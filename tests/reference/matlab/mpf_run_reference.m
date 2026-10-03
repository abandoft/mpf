function mpf_run_reference(project_root, output_root)
% Run native MATLAB evidence; compiler parity is verified separately by CMake.
    actual_release = ['R', version('-release')];
    assert(strcmp(actual_release, 'R2024b'), 'MPF:ReferenceRelease', ...
        'Reference execution requires R2024b, not %s.', actual_release);
    source_revision = getenv('GITHUB_SHA');
    assert(~isempty(regexp(source_revision, '^[0-9a-f]{40}$', 'once')), ...
        'MPF:ReferenceRevision', 'GITHUB_SHA must identify the tested source revision.');
    expected_root = fullfile(project_root, 'build', 'matlab-reference', 'official');
    assert(strcmp(output_root, expected_root), 'MPF:ReferenceOutput', ...
        'Reference evidence must stay beneath root build/matlab-reference/official.');
    if ~isfolder(output_root)
        mkdir(output_root);
    end

    case_name = 'matlab-invocation-context';
    relative_source = 'examples/matlab/invocation_context.m';
    input = fullfile(project_root, relative_source);
    % run() enters the source directory. Execute a byte-identical, correctly named
    % copy without unrelated example files that could shadow MATLAB built-ins.
    snapshot_directory = fullfile(output_root, 'source');
    if ~isfolder(snapshot_directory)
        mkdir(snapshot_directory);
    end
    relative_snapshot = 'source/invocation_context.m';
    snapshot = fullfile(snapshot_directory, 'invocation_context.m');
    copyfile(input, snapshot);
    transcript = execute_source(snapshot);
    write_utf8(fullfile(output_root, [case_name, '.stdout']), transcript);

    provenance = struct('schemaVersion', 1, 'matlabRelease', actual_release, ...
        'matlabVersion', version(), 'sourceRevision', source_revision, ...
        'caseName', case_name, 'source', relative_source, ...
        'sourceSnapshot', relative_snapshot);
    write_utf8(fullfile(output_root, 'provenance.json'), ...
        jsonencode(provenance, 'PrettyPrint', true));

    validator_case = 'matlab-validator-identities';
    validator_source = 'tests/fixtures/matlab_validator_exception_identity.m';
    validator_snapshot = 'source/matlab_validator_exception_identity.m';
    copyfile(fullfile(project_root, validator_source), fullfile(output_root, validator_snapshot));
    validator_transcript = execute_source(fullfile(output_root, validator_snapshot));
    write_utf8(fullfile(output_root, [validator_case, '.stdout']), validator_transcript);
    validator_provenance = struct('schemaVersion', 1, 'matlabRelease', actual_release, ...
        'matlabVersion', version(), 'sourceRevision', source_revision, ...
        'caseName', validator_case, 'source', validator_source, ...
        'sourceSnapshot', validator_snapshot);
    write_utf8(fullfile(output_root, 'validator-parity-provenance.json'), ...
        jsonencode(validator_provenance, 'PrettyPrint', true));

    % These observations cover pending MPF semantics, not implemented parity.
    observations = mpf_output_semantics();
    observations.matlabRelease = actual_release;
    observations.matlabVersion = version();
    observations.sourceRevision = source_revision;
    write_utf8(fullfile(output_root, 'output-semantics.json'), ...
        jsonencode(observations, 'PrettyPrint', true));
    fprintf('Executed %s on %s; recorded %d output-semantic observations.\n', ...
        case_name, actual_release, numel(observations.cases));

    validators = mpf_validator_semantics();
    validators.matlabRelease = actual_release;
    validators.matlabVersion = version();
    validators.sourceRevision = source_revision;
    write_utf8(fullfile(output_root, 'validator-semantics.json'), ...
        jsonencode(validators, 'PrettyPrint', true));
    fprintf('Recorded %d native standard-validator observations.\n', numel(validators.cases));
    fprintf('Recorded %d additional R2024b empty-class observations.\n', ...
        numel(validators.emptyCharacter) + numel(validators.emptyNumeric));
end

function transcript = execute_source(source_path)
% Give the source script its own workspace, including its own ans binding.
    transcript = evalc('run(source_path)');
end

function write_utf8(path, text)
    file = fopen(path, 'wb', 'n', 'UTF-8');
    assert(file >= 0, 'MPF:ReferenceWrite', 'Cannot open reference evidence: %s', path);
    close_file = onCleanup(@() fclose(file));
    bytes = unicode2native(text, 'UTF-8');
    count = fwrite(file, bytes, 'uint8');
    assert(count == numel(bytes), ...
        'MPF:ReferenceWrite', 'Cannot write complete reference evidence: %s', path);
    clear close_file;
end

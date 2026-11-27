addToLibrary({
	$ufa_init__postset: 'ufa_init();',
	$ufa_init: () => {
		const handles = new Map();
		let nextHandle = 0;

		// Refs outlive actions and are addressed separately.
		// Ids start at 1 so that 0 can mean "no ref".
		const refs = new Map();
		let nextRef = 1;

		function allocHandle(obj) {
			const handle = nextHandle++;
			handles.set(handle, obj);
			return handle;
		}

		function closeHandle(handle) {
			handles.delete(handle);
		}

		function resolveHandle(handle) {
			return handles.get(handle);
		}

		const UFA_OK = 0;
		const UFA_PENDING = 1;
		const UFA_CANCELLED = 2;
		const UFA_ERROR = 3;

		function allocString(alloc_ctx, str) {
			const len = lengthBytesUTF8(str) + 1;
			const buf = _ufa_web_malloc(alloc_ctx, len);
			stringToUTF8(str, buf, len);
			return buf;
		}

		function reportError(obj, e) {
			console.error(e);
			setValue(obj.error_ptr, allocString(obj.alloc_ctx, String(e)), '*');
		}

		// Make one of the chosen files the current file and load its content.
		// Only the content of the current file is kept in memory.
		function selectOpenFile(obj, index) {
			const entry = obj.files[index];
			// A newer selection or the end of the action supersedes this load
			const loadId = ++obj.loadId;

			// ufa_web_ref_file refers to the current file through these
			obj.fsHandle = entry.fsHandle;
			obj.filename = entry.file.name;
			obj.content = null;
			obj.pos = 0;

			setValue(obj.filename_ptr, entry.name_ptr, '*');
			setValue(obj.error_ptr, 0, '*');
			setValue(obj.status_ptr, UFA_PENDING, 'i8');

			entry.file.arrayBuffer().then((buf) => {
				if (obj.loadId !== loadId) { return; }

				obj.content = new Uint8Array(buf);
				setValue(obj.status_ptr, UFA_OK, 'i8');
			}).catch((e) => {
				if (obj.loadId !== loadId) { return; }

				reportError(obj, e);
				setValue(obj.status_ptr, UFA_ERROR, 'i8');
			});
		}

		_ufa_web_begin_open_file = (filter, allow_multiple, file_ref, alloc_ctx, filename_ptr, status_ptr, error_ptr) => {
			const accept = UTF8ToString(filter);
			const multiple = allow_multiple !== 0;

			const obj = {
				alloc_ctx,
				filename_ptr,
				status_ptr,
				error_ptr,
				// All the chosen files: { file, fsHandle, name_ptr }
				files: [],
				loadId: 0,
				// The current file
				fsHandle: null,
				filename: null,
				content: null,
				pos: 0,
			};

			// entries is a list of { file, fsHandle }.
			// fsHandle is null when the file was picked without the File System
			// Access API.
			function openSuccess(entries) {
				obj.files = entries.map(({ file, fsHandle }) => ({
					file,
					fsHandle,
					name_ptr: allocString(alloc_ctx, file.name),
				}));

				selectOpenFile(obj, 0);
			}

			function openFailure() {
				setValue(status_ptr, UFA_CANCELLED, 'i8');
			}

			function openWithInput() {
				const input = document.createElement('input');
				input.type = 'file';
				obj.input = input;

				input.addEventListener("change", (event) => {
					if (input.files.length >= 1) {
						openSuccess(Array.from(
							input.files,
							(file) => ({ file, fsHandle: null })
						));
					} else {
						openFailure();
					}
				}, {once: true});

				input.addEventListener("cancel", (event) => {
					openFailure();
				}, {once: true});

				input.accept = accept;
				input.multiple = multiple;
				input.click();
			}

			const ref = file_ref !== 0 ? refs.get(file_ref) : null;
			if (ref) {
				if (ref.fsHandle) {
					Promise.resolve()
						.then(() => ref.fsHandle.queryPermission({ mode: 'read' }))
						.then((permission) => {
							// Only prompt (which needs a user gesture) when the
							// permission is not already granted
							if (permission === 'granted') { return permission; }
							return ref.fsHandle.requestPermission({ mode: 'read' });
						})
						.then((permission) => {
							if (permission !== 'granted') {
								throw new Error('Read permission was not granted');
							}

							// getFile returns a snapshot of the current
							// on-disk content, so this also serves as a reload
							return ref.fsHandle.getFile().then((file) => {
								openSuccess([{ file, fsHandle: ref.fsHandle }]);
							});
						})
						.catch((e) => {
							reportError(obj, e);
							setValue(status_ptr, UFA_ERROR, 'i8');
						});
				} else {
					// The file was originally picked without the File System
					// Access API; its content cannot be read again
					reportError(obj, new Error('Reopening a file is not supported in this browser'));
					setValue(status_ptr, UFA_ERROR, 'i8');
				}
			} else if (window.showOpenFilePicker) {
				const options = { multiple };
				if (accept !== '') {
					options.types = [{
						accept: { 'application/octet-stream': accept.split(',') },
					}];
				}

				window.showOpenFilePicker(options).then(
					(fsHandles) => {
						// Keep the handles so a ref can write back to the file
						return Promise.all(fsHandles.map(
							(fsHandle) => fsHandle.getFile().then((file) => ({ file, fsHandle }))
						)).then(openSuccess).catch((e) => {
							reportError(obj, e);
							setValue(status_ptr, UFA_ERROR, 'i8');
						});
					},
					(e) => {
						if (e.name === 'AbortError') {
							openFailure();
						} else {
							// e.g: SecurityError when called outside of a user
							// gesture or in a cross-origin iframe
							console.error(e);
							openWithInput();
						}
					}
				);
			} else {
				openWithInput();
			}

			return allocHandle(obj);
		};

		_ufa_web_end_open_file = (handle) => {
			const file = resolveHandle(handle);
			// Drop any load still in flight
			if (file) { file.loadId++; }

			closeHandle(handle);
		};

		_ufa_web_get_open_file_count = (handle) => {
			const file = resolveHandle(handle);
			return file ? file.files.length : 0;
		};

		_ufa_web_get_open_file_name_at = (handle, index) => {
			const file = resolveHandle(handle);
			if (!file || index < 0 || index >= file.files.length) { return 0; }

			return file.files[index].name_ptr;
		};

		_ufa_web_select_open_file = (handle, index) => {
			const file = resolveHandle(handle);
			if (!file) { return UFA_ERROR; }

			if (index < 0 || index >= file.files.length) {
				// The current file is left as is
				setValue(
					file.error_ptr,
					allocString(file.alloc_ctx, 'The file index is out of range'),
					'*'
				);
				return UFA_ERROR;
			}

			selectOpenFile(file, index);
			return UFA_PENDING;
		};

		_ufa_web_get_open_file_size = (handle) => {
			const file = resolveHandle(handle);
			// The content is only there once the action is UFA_OK
			return file && file.content ? file.content.byteLength : 0;
		};

		_ufa_web_read_open_file = (handle, buf, size_ptr) => {
			const file = resolveHandle(handle);
			try {
				if (!file || !file.content) {
					if (file) { reportError(file, new Error('The file is not ready for reading')); }
					HEAPU32[size_ptr >> 2] = 0;
					return UFA_ERROR;
				}

				const bytesRequested = HEAPU32[size_ptr >> 2];

				if (file.pos >= file.content.byteLength) {
					HEAPU32[size_ptr >> 2] = 0;
					return UFA_OK;
				}

				const bytesAvailable = file.content.byteLength - file.pos;
				const bytesToRead = Math.min(bytesRequested, bytesAvailable);

				HEAPU8.set(file.content.subarray(file.pos, file.pos + bytesToRead), buf);
				file.pos += bytesToRead;

				HEAPU32[size_ptr >> 2] = bytesToRead;
				return UFA_OK;
			} catch (e) {
				if (file) { reportError(file, e); } else { console.error(e); }
				return UFA_ERROR;
			}
		};

		_ufa_web_get_default_name = (alloc_ctx) => {
			let name = (document.title || '')
				.replace(/[\\/:*?"<>|\x00-\x1f]/g, '_')
				.trim();
			if (name === '') { name = 'ufa'; }

			return allocString(alloc_ctx, name);
		};

		_ufa_web_begin_save_file = (filename, accept, show_dialog, file_ref, alloc_ctx, filename_ptr, status_ptr, error_ptr) => {
			const obj = {
				filename: UTF8ToString(filename),
				alloc_ctx,
				error_ptr,
				fsHandle: null,
				mode: null,
				chunks: [],
				writable: null,
				pending: null,
				error: null,
			};

			function startDownload() {
				obj.mode = 'download';
				setValue(status_ptr, UFA_OK, 'i8');
			}

			function startWritable(fsHandle, writable) {
				obj.mode = 'writable';
				obj.fsHandle = fsHandle;
				obj.writable = writable;
				obj.pending = Promise.resolve();
				setValue(status_ptr, UFA_OK, 'i8');
			}

			const ref = file_ref !== 0 ? refs.get(file_ref) : null;
			if (ref) {
				if (ref.fsHandle) {
					Promise.resolve()
						.then(() => ref.fsHandle.requestPermission({ mode: 'readwrite' }))
						.then((permission) => {
							if (permission !== 'granted') {
								throw new Error('Write permission was not granted');
							}
							return ref.fsHandle.createWritable();
						})
						.then((writable) => {
							startWritable(ref.fsHandle, writable);
						})
						.catch((e) => {
							// Write-back is not possible; degrade to a download
							// named after the file
							console.error(e);
							startDownload();
						});
				} else {
					// The file was opened without the File System Access API
					startDownload();
				}
			} else if (show_dialog && window.showSaveFilePicker) {
				const options = { suggestedName: obj.filename };
				const extensions = UTF8ToString(accept);
				if (extensions !== '') {
					options.types = [{
						accept: { 'application/octet-stream': extensions.split(',') },
					}];
				}

				window.showSaveFilePicker(options).then(
					(handle) => handle.createWritable().then((writable) => {
						// Report the name the user actually chose
						obj.filename = handle.name;
						setValue(filename_ptr, allocString(alloc_ctx, handle.name), '*');

						startWritable(handle, writable);
					}).catch((e) => {
						reportError(obj, e);
						setValue(status_ptr, UFA_ERROR, 'i8');
					}),
					(e) => {
						if (e.name === 'AbortError') {
							setValue(status_ptr, UFA_CANCELLED, 'i8');
						} else {
							// e.g: SecurityError when called outside of a user
							// gesture or in a cross-origin iframe
							console.error(e);
							startDownload();
						}
					}
				);
			} else {
				startDownload();
			}

			return allocHandle(obj);
		};

		_ufa_web_end_save_file = (handle) => {
			const file = resolveHandle(handle);
			if (!file) { return; }

			if (file.writable) {
				file.pending
					.then(() => file.writable.close())
					.catch((e) => { console.error(e); });
			} else if (file.mode === 'download') {
				const blob = new Blob(file.chunks, { type: 'application/octet-stream' });
				const url = URL.createObjectURL(blob);

				const a = document.createElement('a');
				a.href = url;
				a.download = file.filename;
				a.click();

				URL.revokeObjectURL(url);
			}

			closeHandle(handle);
		};

		_ufa_web_write_save_file = function(handle, buf, size_ptr) {
			const file = resolveHandle(handle);
			try {
				if (!file || file.error) { return UFA_ERROR; }

				const bytesToWrite = HEAPU32[size_ptr >> 2];
				const chunk = HEAPU8.slice(buf, buf + bytesToWrite);
				if (file.writable) {
					// The stream serializes writes; a failure surfaces on the
					// next call
					file.pending = file.pending
						.then(() => file.writable.write(chunk))
						.catch((e) => {
							reportError(file, e);
							file.error = e;
						});
				} else {
					file.chunks.push(chunk);
				}
				return UFA_OK;
			} catch (e) {
				if (file) { reportError(file, e); } else { console.error(e); }
				return UFA_ERROR;
			}
		};

		_ufa_web_ref_file = (handle) => {
			const obj = resolveHandle(handle);
			if (!obj || !obj.filename) { return 0; }

			const ref = nextRef++;
			refs.set(ref, {
				fsHandle: obj.fsHandle || null,
				name: obj.filename,
			});
			return ref;
		};

		_ufa_web_release_file_ref = (ref) => {
			refs.delete(ref);
		};
	},

	ufa_web_begin_open_file: () => {},
	ufa_web_begin_open_file__deps: ['$ufa_init'],

	ufa_web_end_open_file: () => {},
	ufa_web_end_open_file__deps: ['$ufa_init'],

	ufa_web_get_open_file_count: () => {},
	ufa_web_get_open_file_count__deps: ['$ufa_init'],

	ufa_web_get_open_file_name_at: () => {},
	ufa_web_get_open_file_name_at__deps: ['$ufa_init'],

	ufa_web_select_open_file: () => {},
	ufa_web_select_open_file__deps: ['$ufa_init'],

	ufa_web_get_open_file_size: () => {},
	ufa_web_get_open_file_size__deps: ['$ufa_init'],

	ufa_web_read_open_file: () => {},
	ufa_web_read_open_file__deps: ['$ufa_init'],

	ufa_web_get_default_name: () => {},
	ufa_web_get_default_name__deps: ['$ufa_init'],

	ufa_web_begin_save_file: () => {},
	ufa_web_begin_save_file__deps: ['$ufa_init'],

	ufa_web_end_save_file: () => {},
	ufa_web_end_save_file__deps: ['$ufa_init'],

	ufa_web_write_save_file: () => {},
	ufa_web_write_save_file__deps: ['$ufa_init'],

	ufa_web_ref_file: () => {},
	ufa_web_ref_file__deps: ['$ufa_init'],

	ufa_web_release_file_ref: () => {},
	ufa_web_release_file_ref__deps: ['$ufa_init'],
});

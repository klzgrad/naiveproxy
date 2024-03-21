package deps

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"os"
	"path"
)

func fetchIntoFile(url string, file *os.File) error {
	resp, err := http.Get(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	_, err = io.Copy(file, resp.Body)
	return err
}

func fileSHA256(file *os.File) (string, error) {
	file.Seek(0, 0)
	hasher := sha256.New()
	if _, err := io.Copy(hasher, file); err != nil {
		return "", nil
	}
	return hex.EncodeToString(hasher.Sum(nil)), nil
}

// FetchURL fetches the specified URL into the specified file path, and returns
// the SHA-256 hash of the file fetched.
func FetchURL(url string, path string) (string, error) {
	file, err := os.Create(path)
	if err != nil {
		return "", err
	}
	defer file.Close()

	if err = fetchIntoFile(url, file); err != nil {
		os.Remove(path)
		return "", err
	}

	checksum, err := fileSHA256(file)
	if err != nil {
		os.Remove(path)
		return "", err
	}

	return checksum, nil
}

// FetchEntry retrieves an existing WORKSPACE file entry into a specified directory,
// verifies its checksum, and then returns the full path to the resulting file.
func FetchEntry(entry *Entry, dir string) (string, error) {
	filename := path.Join(dir, entry.SHA256+".tar.gz")
	checksum, err := FetchURL(entry.URL, filename)
	if err != nil {
		return "", err
	}

	if checksum != entry.SHA256 {
		os.Remove(filename)
		return "", fmt.Errorf("SHA-256 mismatch: expected %s, got %s", entry.SHA256, checksum)
	}

	return filename, nil
}

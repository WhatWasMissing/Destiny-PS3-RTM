package rtmclient

import (
	"context"
	"encoding/hex"
	"errors"
	"fmt"
	"strings"
	"time"
)

const destiny0124ProfileCacheTTL = 5 * time.Second

type memoryReadFunc func(context.Context, string, string, int) ([]byte, error)

type memorySignature struct {
	address uint32
	hex     string
}

type BuildSignatureStatus struct {
	Address     string `json:"address"`
	ExpectedHex string `json:"expected_hex"`
	ObservedHex string `json:"observed_hex,omitempty"`
	Matches     bool   `json:"matches"`
	Error       string `json:"error,omitempty"`
}

type BuildProfileStatus struct {
	Profile    string                 `json:"profile"`
	Matches    bool                   `json:"matches"`
	Signatures []BuildSignatureStatus `json:"signatures"`
}

var destiny0124Signatures = [...]memorySignature{
	{address: 0x00444B64, hex: "3C600044C03601283CC03F803B4349EC3061034462C4000063850000C05A0000"},
	{address: 0x001FB3C8, hex: "8084001C908300004E800020"},
	{address: 0x002FF2D0, hex: "7C0802A648A99751F821FF81F8010090"},
}

// ValidateDestinyWriteRange prevents raw writes from overwriting the bytes used
// to recognize the supported runtime, which would disable later safety checks.
func ValidateDestinyWriteRange(address uint32, length int) error {
	if length <= 0 {
		return errors.New("memory write range must be non-empty")
	}
	end := uint64(address) + uint64(length)
	for _, signature := range destiny0124Signatures {
		start := uint64(signature.address)
		signatureEnd := start + uint64(len(signature.hex)/2)
		if uint64(address) < signatureEnd && start < end {
			return fmt.Errorf("write overlaps protected 01.24 code signature at 0x%08X", signature.address)
		}
	}
	return nil
}

// InspectDestiny0124SignatureProfile reads three independent code sites that
// match the audited BLES01857 01.24 ELF. A match is a compatibility signal, not
// proof of the whole EBOOT hash or title-region ID.
func InspectDestiny0124SignatureProfile(ctx context.Context, target string, read memoryReadFunc) BuildProfileStatus {
	status := BuildProfileStatus{Profile: "BLES01857 01.24 signature profile", Matches: read != nil}
	for _, signature := range destiny0124Signatures {
		address := fmt.Sprintf("0x%08X", signature.address)
		item := BuildSignatureStatus{Address: address, ExpectedHex: signature.hex}
		if read == nil {
			item.Error = "memory reader is unavailable"
			status.Matches = false
			status.Signatures = append(status.Signatures, item)
			continue
		}
		if err := ctx.Err(); err != nil {
			item.Error = err.Error()
			status.Matches = false
			status.Signatures = append(status.Signatures, item)
			continue
		}
		data, err := read(ctx, target, address[2:], len(signature.hex)/2)
		if err != nil {
			item.Error = err.Error()
			status.Matches = false
			status.Signatures = append(status.Signatures, item)
			continue
		}
		observed := strings.ToUpper(hex.EncodeToString(data))
		item.ObservedHex = observed
		item.Matches = observed == signature.hex
		status.Matches = status.Matches && item.Matches
		status.Signatures = append(status.Signatures, item)
	}
	return status
}

// VerifyDestiny0124SignatureProfile refuses a write unless all three signature
// sites match the audited profile.
func VerifyDestiny0124SignatureProfile(ctx context.Context, target string, read memoryReadFunc) error {
	status := InspectDestiny0124SignatureProfile(ctx, target, read)
	if status.Matches {
		return nil
	}
	for _, signature := range status.Signatures {
		if signature.Error != "" {
			return fmt.Errorf("cannot verify Destiny 01.24 signature at %s: %s", signature.Address, signature.Error)
		}
		if !signature.Matches {
			return fmt.Errorf("unsupported Destiny build: 01.24 signature mismatch at %s; memory write refused", signature.Address)
		}
	}
	return errors.New("unsupported Destiny build; memory write refused")
}
